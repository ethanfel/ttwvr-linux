// SPDX-License-Identifier: GPL-3.0-only
#include "LiveSettings.h"
#include <cassert>
using namespace ttwvrnr;
int main() {
  ReloadState r;
  LiveSettings off, nr, both, dlaa;
  nr.neural = nr.temporal = 1;
  nr.identity = 0;
  both = nr;
  both.dlaa = 1;
  dlaa = both;
  dlaa.neural = 0;
  assert(r.Request(off, true) && !r.NeedStart());
  assert(r.Request(nr) && r.NeedStart());
  r.Started();
  assert(!r.CanApply() && !r.NeedStart());
  r.Initialized();
  assert(r.CanApply());
  assert(!r.Request(nr)); // polling identical config never resets history
  // Change while starting/shutting down; the newest settings win, but an old
  // acknowledgement cannot enable jitter and no second helper can start.
  assert(r.Request(both) && r.NeedStop() && !r.CanApply());
  assert(r.Request(dlaa) && !r.NeedStart());
  r.Initialized();
  assert(!r.CanApply());
  r.Exited();
  assert(r.NeedStart() && r.requested == dlaa);
  r.Started();
  r.Initialized();
  assert(r.CanApply());
  assert(r.Request(off) && r.NeedStop() && !r.CanApply());
  r.Exited();
  assert(!r.NeedStart() && !r.fault);
  r.Request(both);
  r.Started();
  r.Fail();
  assert(r.NeedStop() && !r.CanApply());
  r.Exited();
  assert(!r.NeedStart());
  assert(!r.Request(both)); // a failed generation never retries every frame
  assert(r.Request(both, true) && r.NeedStart());
  r.Started();
  r.Initialized();
  r.Exited(); // unexpected helper death
  assert(r.fault && !r.CanApply() && !r.NeedStart());
  // SR changes transport dimensions. An old generation must release its GPU
  // resources before starting SR, even when multiple requests coalesce.
  LiveSettings sr = dlaa;
  sr.dlaa = 0;
  sr.sr = 1;
  assert(sr.Valid());
  assert(r.Request(sr, true) && r.NeedStart());
  r.Started();
  r.Initialized();
  assert(r.CanApply());
  assert(r.Request(dlaa) && r.NeedStop() && !r.CanApply());
  assert(r.Request(sr) && !r.NeedStart());
  r.Initialized();
  assert(!r.CanApply());
  r.Exited();
  assert(r.NeedStart() && r.requested.sr);
  r.Started();
  r.Initialized();
  r.Fail();
  r.Exited();
  assert(r.fault && !r.CanApply());
  auto invalidSR = sr;
  invalidSR.neural = 1;
  assert(!invalidSR.Valid());
  invalidSR = sr;
  invalidSR.dlaa = 1;
  assert(!invalidSR.Valid());
  invalidSR = sr;
  invalidSR.temporal = 0;
  assert(!invalidSR.Valid());
  auto bad = both;
  bad.resolution = 0;
  assert(!r.Request(bad, true) && r.fault);
  bad = both;
  bad.temporal = 0;
  assert(!bad.Valid());
}
