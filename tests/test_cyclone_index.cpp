#include "CycloneTrackIndex.h"
#include <cassert>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <limits>
using climatology::CycloneTrackIndex;
int main() {
  CycloneTrackIndex index;
  index.Add(0, 0, 1, 1, 319);
  assert(index.Crossings(.2, .3, .4, .5, 319, 30) == 0);
  assert(index.Crossings(.2, .4, .4, .2, 319, 30) == 1);
  assert(index.Crossings(.5, .7, .5, .7, 319, 30) == 0);
  assert(index.Crossings(.5, .5, .5, .5, 319, 30) == 1);
  assert(index.Crossings(1, 1, 2, 2, 319, 30) == 1);
  assert(index.Crossings(.2, .2, .4, .4, 319, 30) == 1);
  assert(index.Crossings(2, 2, 3, 3, 319, 30) == 0);
  assert(index.Crossings(.2, .4, .4, .2, 100, 30) == 0);
  assert(index.Crossings(.2, .4, .4, .2, 100, 390) == 1);
  const auto bins = index.CellCount();
  for (int i = 0; i < 10000; ++i)
    assert(index.Crossings(60, 40, 60.1, 40.1, 319, 30) == 0);
  assert(index.CellCount() == bins);
  CycloneTrackIndex date;
  date.Add(10, 179.9, 11, -179.9, 363);
  assert(date.Crossings(10.5, 179.8, 10.5, -179.8, 2, 30) == 1);
  assert(date.Crossings(10.5, -180.2, 10.5, -179.8, 2, 30) == 1);
  assert(date.Crossings(10.5, -.2, 10.5, .2, 2, 30) == 0);
  CycloneTrackIndex negative;
  negative.Add(-.8, -.8, -.2, -.2, 319);
  assert(negative.Crossings(-.8, -.2, -.2, -.8, 319, 30) == 1);
  assert(index.Crossings(0, 0, 1, 1, 319, -1) == -1);
  assert(index.Crossings(std::numeric_limits<double>::infinity(), 0, 1, 1, 319,
                         30) == -1);
  CycloneTrackIndex empty;
  assert(empty.Crossings(0, 0, 1, 1, 319, 30) == 0);
  // Readers retain their immutable snapshot while a new filtered index is
  // published.
  std::shared_ptr<const CycloneTrackIndex> current =
      std::make_shared<CycloneTrackIndex>(index);
  std::atomic<bool> failed{false};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i)
    readers.emplace_back([&] {
      for (int n = 0; n < 10000; ++n) {
        auto snapshot = std::atomic_load(&current);
        const int value = snapshot->Crossings(.2, .4, .4, .2, 319, 30);
        if (value != 0 && value != 1) failed = true;
      }
    });
  for (int n = 0; n < 100; ++n) {
    std::shared_ptr<const CycloneTrackIndex> replacement =
        n % 2 ? std::make_shared<CycloneTrackIndex>(index)
              : std::make_shared<CycloneTrackIndex>();
    std::atomic_store(&current, replacement);
  }
  for (auto& reader : readers) reader.join();
  assert(!failed.load());
}
