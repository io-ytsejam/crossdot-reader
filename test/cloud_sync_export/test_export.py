"""Exercise the real firmware serializer without ESP-IDF networking.

The pure anonymous-namespace export section is compiled verbatim, not copied
or mocked. Python's strict JSON decoder validates its output across day visits.
Run: python3 -m unittest discover -s test/cloud_sync_export -v
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CloudSyncExportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="cloud-sync-export-")
        work = Path(cls.work.name)
        source = (ROOT / "src/network/CloudSyncService.cpp").read_text()
        cap = source[source.index("constexpr size_t MAX_EXPORT_BYTES"):].splitlines()[0]
        begin = source.index("// Lightweight JSON string escaper")
        end = source.index('// Precise elapsed-time string', begin)
        harness = '''
#include <cstdio>
#include <string>
#include <iostream>
#include "StatisticsStore.h"
'''+cap+'\n'+source[begin:end]+r'''
int main(int argc, char**) {
  std::string out = "{\"schemaVersion\":2,\"days\":[";
  ExportSink sink;
  sink.out = &out;
  DailyReadingStatistics day;
  day.date = "2026-09-23";
  day.totalSeconds = 123;
  day.books.reserve(1);
  BookReadingStatistics book;
  book.title = "Quote \" slash \\ newline\n tab\t control\x01";
  book.author = "Zażółć";
  book.activeSeconds = 123;
  day.books.push_back(book);
  for (int i = 1; i < argc; ++i) {
    if (!writeDay(&sink, day)) return 2;
    day.date = "2026-09-24";
    day.books.clear();
  }
  out += "]}";
  std::cout << out;
}
'''
        cpp = work / 'export.cpp'
        cpp.write_text(harness)
        cls.binary = work / 'export-test'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        '-I'+str(ROOT / 'src'), str(cpp), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def export(self, count):
        result = subprocess.run([str(self.binary)] + ['day'] * count,
                                capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def test_empty_history(self):
        self.assertEqual(self.export(0)['days'], [])

    def test_single_day_escapes_strings(self):
        day = self.export(1)['days'][0]
        self.assertEqual(day['books'][0]['title'], 'Quote " slash \\ newline\n tab\t control\x01')
        self.assertEqual(day['books'][0]['author'], 'Zażółć')
        self.assertEqual(day['books'][0]['activeSeconds'], 123)

    def test_multiple_days_are_separated(self):
        days = self.export(2)['days']
        self.assertEqual([d['date'] for d in days], ['2026-09-23', '2026-09-24'])
        self.assertEqual(days[1]['books'], [])


if __name__ == '__main__':
    unittest.main()
