window.BENCHMARK_DATA = {
  "lastUpdate": 1773175472084,
  "repoUrl": "https://github.com/mbaldessari/zipwrapper",
  "entries": {
    "Benchmark": [
      {
        "commit": {
          "author": {
            "email": "michele@acksyn.org",
            "name": "Michele Baldessari",
            "username": "mbaldessari"
          },
          "committer": {
            "email": "michele@acksyn.org",
            "name": "Michele Baldessari",
            "username": "mbaldessari"
          },
          "distinct": true,
          "id": "11c0b26e13945061baad47703984decb660d7f6b",
          "message": "Try and fix the benchmarking gh",
          "timestamp": "2026-03-10T21:43:47+01:00",
          "tree_id": "a23f589400869085523044c4f1a006126078893b",
          "url": "https://github.com/mbaldessari/zipwrapper/commit/11c0b26e13945061baad47703984decb660d7f6b"
        },
        "date": 1773175471163,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "ZipRoundTripTest.MultipleEntries",
            "value": 8200175,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 8200175 ns\nthreads: undefined"
          },
          {
            "name": "ZipRoundTripTest.BinaryContent",
            "value": 10061315,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 10061315 ns\nthreads: undefined"
          },
          {
            "name": "ZipRoundTripTest.WriteToOstream",
            "value": 7842272,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 7842272 ns\nthreads: undefined"
          },
          {
            "name": "ZipInputStreamTest.BinaryRoundTrip",
            "value": 8015577,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 8015577 ns\nthreads: undefined"
          },
          {
            "name": "ZipFileDataTest.GetInputStreamByName",
            "value": 7819184,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 7819184 ns\nthreads: undefined"
          },
          {
            "name": "ZipFileDataTest.CloneCreatesIndependentCopy",
            "value": 7834659,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 7834659 ns\nthreads: undefined"
          },
          {
            "name": "GZIPTest.LargeData",
            "value": 35795911,
            "unit": "ns/iter",
            "extra": "iterations: 1\ncpu: 35795911 ns\nthreads: undefined"
          }
        ]
      }
    ]
  }
}