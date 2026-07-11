#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

#include "CRC32.hpp"
#include "IO.hpp"

bool FIND_VALUE = false;
std::mutex m;

/// @brief Переписывает последние 4 байта значением value
void replaceLastFourBytes(std::vector<char> &data, uint32_t value) {
  std::copy_n(reinterpret_cast<const char *>(&value), 4, data.end() - 4);
}

void hackEngine(std::vector<char> result, size_t idxStart, size_t idxEnd,
  uint32_t originalCrc32, std::vector<char>& res_ret, uint32_t prev = 0xFFFFFFFF,
  bool isPrintOut = false)
{
  int lastPct = -1;
  for (size_t i = idxStart; i < idxEnd; i++) {

    // Заменяем последние четыре байта на значение i
    replaceLastFourBytes(result, uint32_t(i));
    // Вычисляем CRC32 текущего вектора result
    auto currentCrc32 = crc32(result.data(), result.size(), prev);

    if (currentCrc32 == originalCrc32) {
      std::cout << "Success\n";
      res_ret = result;
      m.lock();
      FIND_VALUE = true;
      m.unlock();
      return;
    }
    if (FIND_VALUE) return;
    // Отображаем прогресс
    if (isPrintOut) {
      int pct = static_cast<int>((i - idxStart) * 100 / (idxEnd - idxStart));
      if (pct != lastPct && pct % 10 == 0) {
        lastPct = pct;
        std::cout << "progress thread: " << std::this_thread::get_id() << " "
                  << pct << " %" << std::endl;
      }
    }
  }
}

unsigned int getDynamicNumThreads()
{
  const unsigned int NUM_TEST = 10000000;
  unsigned int n = std::thread::hardware_concurrency();
  std::vector<float> vTimeTest(n);
  size_t idxMin = 0;
  std::vector<char> testSequence = {'T', 'E', 'S', 'T', 'C', 'R', 'S', 'F', 'U', 'N', 'C',
    'T', 'I', 'O', 'N'};
  float min = std::numeric_limits<float>::max();
  for (size_t i = 0; i < vTimeTest.size(); i++) {
    std::vector<std::thread> vThread;
    auto start = std::chrono::steady_clock::now();
    std::vector<char> retRes{' '};
    for (size_t num = 0; num < i+1; num++) {
      vThread.emplace_back(
        hackEngine, testSequence, static_cast<size_t>(num * NUM_TEST / (i + 1)),
          static_cast<size_t>((num+1) * NUM_TEST / (i + 1)), 1, std::ref(retRes),
          0xFFFFFFFF, false
      );
    }

    for (size_t j = 0; j < vThread.size(); j++) {
      vThread[j].join();
    }
    auto end = std::chrono::steady_clock::now();
    auto timeTesting = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    vTimeTest[i] = static_cast<float>(timeTesting.count()) / static_cast<float>(NUM_TEST);

    if (vTimeTest[i] < min) {
      min = vTimeTest[i];
      idxMin = i;
    }
  }

  std::cout << "-------------------------------" << std::endl;
  std::cout << "|\tTHREAD DIAGNOSTIC:\t|" << std::endl;
  for (size_t i = 0; i < n; i++) {
    std::cout << "| # " << i+1 << " " << vTimeTest[i] << " milliseconds\t" << "|" << std::endl;
  }
  std::cout << "| min time with treads = " << idxMin + 1 << "\t|" << std::endl;
  std::cout << "-------------------------------" << std::endl;
  return static_cast<unsigned int>(idxMin + 1);

}

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

  std::vector<char> result(original.size() + injection.size() + 4);
  auto it = std::copy(original.begin(), original.end(), result.begin());
  const uint32_t prevCrc = crc32(result.data(), result.size() - 4);
  std::copy(injection.begin(), injection.end(), it);

  /*
   * Внимание: код ниже крайне не оптимален.
   * В качестве доп. задания устраните избыточные вычисления
   */
  const size_t maxVal = std::numeric_limits<uint32_t>::max();
  std::vector<char> ret_res{};

  std::vector<std::thread> v_thread{};
  // unsigned int num_thread = std::thread::hardware_concurrency();
  unsigned int num_thread = getDynamicNumThreads();
  auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < num_thread; i++) {
    v_thread.emplace_back(
      hackEngine, result, static_cast<size_t>(i * maxVal / num_thread),
        static_cast<size_t>((i+1) * maxVal / num_thread), originalCrc32, std::ref(ret_res),
        prevCrc, true
    );
  }

  for (size_t i = 0; i < v_thread.size(); i++) {
    v_thread[i].join();
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
  std::cout << "Time hack: " << elapsed.count() << " seconds" << std::endl;

  if (ret_res.size() == 0) throw std::logic_error("Can't hack");

  return ret_res;
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
