#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

#include "CRC32.hpp"
#include "IO.hpp"

size_t CHECK_INTERVAL = 100000;

/// @brief Переписывает последние 4 байта значением value
void replaceLastFourBytes(std::vector<char> &data, uint32_t value) {
  std::copy_n(reinterpret_cast<const char *>(&value), 4, data.end() - 4);
}

/**
 * @brief struct for managment common thread params
 */
struct ThreadManager {
  std::mutex m_mutex{};
  bool m_find{ false };
};

/**
 * @brief params for one thread task
 */
struct TaskParams
{
  size_t m_idx_start{ 0 };
  size_t m_idx_end{ 0 };
  // std::vector<char> m_data{}; // нужно хранить tail_data, чтобы не тратить время на копирование всего
  uint32_t m_originalCrc32;
  std::reference_wrapper<ThreadManager> m_manager;
  std::reference_wrapper<std::vector<char>> m_ret_res;
  // std::vector<char> m_ret_res{' '};
  uint32_t m_prev;
  bool m_print_progress{ false };

  TaskParams(size_t idx_start, size_t idx_end, uint32_t originalCrc32,
    ThreadManager& manager, std::vector<char>& ret_res, uint32_t prev, bool print_progress = false):
      m_idx_start{idx_start}, m_idx_end{idx_end}, m_originalCrc32{originalCrc32},
      m_manager{manager}, m_ret_res{ret_res}, m_prev{prev},
      m_print_progress{print_progress}
      {}
    inline std::mutex& get_mutex()
    {
      return m_manager.get().m_mutex;
    }
    inline void lock_mutex()
    {
      m_manager.get().m_mutex.lock();
    }
    inline void unlock_mutex()
    {
      m_manager.get().m_mutex.unlock();
    }
    inline bool& get_find_state()
    {
      return m_manager.get().m_find;
    }
    inline void set_find_state()
    {
      m_manager.get().m_find = true;
    }
    inline void set_ret_res(std::vector<char>&& data) {
      std::vector<char>& out = m_ret_res.get();
      out = std::move(data);
    }
};

/**
 * @brief function for resource distribution between threads
 */
std::vector<TaskParams> split_task_to_threads(uint32_t prev, uint32_t originalCrc32,
  ThreadManager& manager, size_t max_value, std::vector<char>& ret_res,
  unsigned int num_thread = 1, bool print_progress = false)
{
  std::vector<TaskParams> v_params{};

  for (unsigned int i = 0; i < num_thread; i++) {
    size_t idx_start = i * max_value / num_thread;
    size_t idx_end = (i + 1) * max_value / num_thread + 1;

    v_params.emplace_back(
      idx_start, idx_end, originalCrc32, manager, ret_res, prev, print_progress
    );
  }

  return v_params;
}

// void hackEngine(std::vector<char> result, size_t idxStart, size_t idxEnd,
//   uint32_t originalCrc32, std::vector<char>& res_ret, ThreadManager& manager,
//   uint32_t prev = 0xFFFFFFFF, bool isPrintOut = false)
void hackEngine(TaskParams& params)
{
  int lastPct = -1;

  std::vector<char> data(4, ' '); // создание начального значения как 4-е пустых значения

  for (size_t i = params.m_idx_start; i < params.m_idx_end; i++) {
    // Заменяем последние четыре байта на значение i
    // replaceLastFourBytes(result, uint32_t(i));
    replaceLastFourBytes(data, uint32_t(i));
    // Вычисляем CRC32 текущего вектора result (только для последних 4-ех значений)
    // std::vector<char> tail_data {result.end() - 4, result.end()};

    // auto currentCrc32 = crc32(result.data(), result.size(), prev);
    // auto currentCrc32 = crc32(tail_data.data(), tail_data.size(), params.m_prev);
    auto currentCrc32 = crc32(data.data(), data.size(), params.m_prev);

    if (currentCrc32 == params.m_originalCrc32) {
      params.lock_mutex();
      std::cout << "Success\n";
      params.set_ret_res(std::move(data));
      params.set_find_state();
      params.unlock_mutex();
      return;
    }

    // проверка остановки всех потоков раз в CHECK_INTERVAL итераций, чтобы не тратить много времени
    // на постоянную блокировку и разблокировку данных
    if (i % CHECK_INTERVAL == 0) {
      params.lock_mutex();
      if (params.get_find_state())
      {
        params.unlock_mutex();
        return;
      }
      params.unlock_mutex();
    }

    // Отображаем прогресс
    if (params.m_print_progress) {
      int pct = static_cast<int>((i - params.m_idx_start) * 100 / (params.m_idx_end - params.m_idx_start));
      if (pct != lastPct && pct % 10 == 0) {
        lastPct = pct;
        std::cout << "progress thread: " << std::this_thread::get_id() << " "
                  << pct << " %" << std::endl;
      }
    }
  }
}

// unsigned int getDynamicNumThreads()
// {
//   const unsigned int NUM_TEST = 10000000;
//   unsigned int n = std::thread::hardware_concurrency();
//   // std::vector<float> vTimeTest(n);
//   std::vector<std::pair<unsigned int, float>> vTimeTest{};
//   size_t idxMin = 0;
//   std::vector<char> testSequence = {'T', 'E', 'S', 'T', 'C', 'R', 'S', 'F', 'U', 'N', 'C',
//     'T', 'I', 'O', 'N'};
//   float min = std::numeric_limits<float>::max();
//   for (size_t i = 1; i <= n; i *= 2) {
//     std::vector<std::thread> vThread;
//     auto start = std::chrono::steady_clock::now();
//     std::vector<char> retRes{' '};
//     // ToDo: для перебора всех чётных мб тогда вместо ++ делать *=2?
//     ThreadManager manager;
//     // for (size_t num = 0; num < i+1; num++) {
//     for (size_t num = 0; num < i + 1; num *= 2) {
//     // ToDo:: нужно ли тут +1 для решения 4-го замечания
//       size_t idx_start = num * NUM_TEST / i;
//       size_t idx_end = (num + 1) * NUM_TEST / i + 1;
//       vThread.emplace_back(
//         hackEngine, testSequence, idx_start, idx_end, 1, std::ref(retRes),
//           std::ref(manager), 0xFFFFFFFF, false
//       );
//     }

//     for (size_t j = 0; j < vThread.size(); j++) {
//       vThread[j].join();
//     }

//     auto end = std::chrono::steady_clock::now();
//     auto timeTesting = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
//     vTimeTest.push_back(std::make_pair(i, static_cast<float>(timeTesting.count()) / static_cast<float>(NUM_TEST)));

//     if (vTimeTest[i].second < min) {
//       min = vTimeTest[i].second;
//       idxMin = i;
//     }
//   }

//   std::cout << "-------------------------------" << std::endl;
//   std::cout << "|\tTHREAD DIAGNOSTIC:\t|" << std::endl;
//   for (size_t i = 0; i < n; i++) {
//     std::cout << "| # " << i+1 << " " << vTimeTest[i] << " milliseconds\t" << "|" << std::endl;
//   }
//   std::cout << "| min time with treads = " << idxMin + 1 << "\t|" << std::endl;
//   std::cout << "-------------------------------" << std::endl;
//   return static_cast<unsigned int>(idxMin + 1);

// }

/**
 * @brief Формирует новый вектор с тем же CRC32, добавляя в конец оригинального
 * строку injection и дополнительные 4 байта
 * @details При формировании нового вектора последние 4 байта не несут полезной
 * нагрузки и подбираются таким образом, чтобы CRC32 нового и оригинального
 * вектора совпадали
 * @param original оригинальный вектор
 * @param injection произвольная строка, которая будет добавлена после данных
 * оригинального вектора
 * @return новый вектор
 */
std::vector<char> hack(const std::vector<char> &original,
                       const std::string &injection) {
  const uint32_t originalCrc32 = crc32(original.data(), original.size());

  // std::vector<char> result(original.size() + injection.size() + 4);
  // std::vector<char> result(original.size() + injection.size() + 4);
  std::vector<char> result(original.size() + injection.size());
  auto it = std::copy(original.begin(), original.end(), result.begin());
  std::copy(injection.begin(), injection.end(), it);
  // const uint32_t prevCrc = crc32(result.data(), result.size() - 4); // вычисление crc32 без последних 4 символов
  const uint32_t prevCrc = crc32(result.data(), result.size());

  const size_t max_val = std::numeric_limits<uint32_t>::max();

  std::vector<std::thread> v_thread{};
  unsigned int num_thread = std::thread::hardware_concurrency();
  // unsigned int num_thread = getDynamicNumThreads();
  // unsigned int num_thread = 1;
  auto start = std::chrono::steady_clock::now();

  ThreadManager manager;
  std::vector<char> ret_data; // контейнер для возвращения результата со всех потоков
  std::vector<TaskParams> v_params = split_task_to_threads(prevCrc, originalCrc32,
    manager, max_val, ret_data, num_thread, true);

  for (size_t i = 0; i < v_params.size(); i++) {
    v_thread.emplace_back(
      hackEngine, std::ref(v_params[i])
    );
  }

  // for (size_t i = 0; i < num_thread; i++) {
  //   v_thread.emplace_back(
  //     hackEngine, result, static_cast<size_t>(i * maxVal / num_thread),
  //       static_cast<size_t>((i+1) * maxVal / num_thread), originalCrc32, std::ref(ret_res),
  //       std::ref(manager), prevCrc, true
  //   );
  // }

  for (size_t i = 0; i < v_thread.size(); i++) {
    v_thread[i].join();
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
  std::cout << "Time hack: " << elapsed.count() << " seconds" << std::endl;

  if (ret_data.size() == 0) throw std::logic_error("Can't hack");

  for (size_t i = 0; i < ret_data.size(); i++) result.push_back(ret_data[i]);

  return result;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Call with two args: " << argv[0]
              << " <input file> <output file>\n";
    return 1;
  }

  try {
    const std::vector<char> data = readFromFile(argv[1]);
    const std::vector<char> badData = hack(data, "He-he-he");
    writeToFile(argv[2], badData);
  } catch (std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 2;
  }
  return 0;
}
