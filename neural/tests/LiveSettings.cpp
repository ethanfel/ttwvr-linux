// SPDX-License-Identifier: GPL-3.0-only
#include "LiveSettings.h"
#include <cassert>
#include <initializer_list>
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
  // Each preset changes input allocation and must invalidate the old helper.
  ReloadState modes;
  for (unsigned mode : {1u, 2u, 3u, 1u, 3u, 2u, 0u}) {
    auto next = sr;
    next.sr = mode;
    assert(next.Valid() && modes.Request(next));
    assert(!modes.CanApply());
    if (modes.process) {
      assert(modes.NeedStop() && !modes.NeedStart());
      modes.Initialized(); // stale acknowledgement must not enable output
      assert(!modes.CanApply());
      modes.Exited();
    }
    if (mode) {
      assert(modes.NeedStart());
      modes.Started();
      modes.Initialized();
      assert(modes.CanApply() && modes.requested.sr == mode);
    } else assert(!modes.NeedStart());
  }
  invalidSR = sr;
  invalidSR.sr = 4;
  assert(!invalidSR.Valid() && !modes.Request(invalidSR));
  assert(SRInputSize(0).width == 2944 && SRInputSize(0).height == 2208);
  assert(SRJitterPhases(0) == 8 && SRJitterPhases(1) == 18);
  assert(SRJitterPhases(2) == 24 && SRJitterPhases(3) == 32);
  assert(SRInputSize(2).width == 1708 && SRInputSize(2).height == 1281);
  assert(SRInputSize(3).width == 1472 && SRInputSize(3).height == 1104);
  assert(SRInputSize(4).width == 0 && SRJitterPhases(4) == 0);
  LiveSettings fg=off;fg.fg=fg.temporal=1;fg.identity=0;
  assert(fg.Valid()&&fg.Active());
  assert(r.Request(fg,true)&&r.NeedStart());r.Started();r.Initialized();
  auto srfg=sr;srfg.fg=1;assert(srfg.Valid());
  assert(r.Request(srfg)&&r.NeedStop()&&!r.CanApply());r.Exited();
  assert(r.NeedStart());r.Started();r.Initialized();assert(r.CanApply());
  auto invalidFG=fg;invalidFG.temporal=0;assert(!invalidFG.Valid());
  invalidFG=fg;invalidFG.identity=1;assert(!invalidFG.Valid());
  r.Fail();r.Exited();
  auto bad = both;
  bad.resolution = 0;
  assert(!r.Request(bad, true) && r.fault);
  bad = both;
  bad.temporal = 0;
  assert(!bad.Valid());
}
