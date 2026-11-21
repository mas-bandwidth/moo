# moo-analyze corrupts its heap on its default worker threads

Status: open, not fixed. Recorded 2026-11-21. The roadmap row is `defects/analyzer-threads` in
`docs/roadmap.sexp`.

## Symptom

`moo-analyze` at v1.3.0 sometimes dies while it checks files on its default worker threads. The
process is killed by libmalloc (SIGTRAP or SIGABRT) for heap corruption on a worker thread inside
`Luau::check`. With `-j1` (one thread) it has not crashed, and it still reports real type errors.

## Measurement (2026-11-21, macOS 27.2, Apple silicon)

Binary: the v1.3.0 release `moo-analyze`. Input: the 11 test files of mas-bandwidth/serialize.moo,
each analyzed in its own process, repeatedly.

| threads           | runs | crashes |
|-------------------|------|---------|
| default           | 380  | 5       |
| `-j1`             | 440  | 0       |

About 1 in 7 CI runs of serialize.moo went red from this (each run calls the analyzer about 12
times).

## Crash reports

macOS crash reports named `moo-analyze-<date>.ips`: one in `/Library/Logs/DiagnosticReports`
(2026-11-21 08:29:00) and others in `~/Library/Logs/DiagnosticReports` from 2026-11-14 to
2026-11-21. The top frames of the crashing thread, copied from the reports:

`moo-analyze-2026-11-21-082900.ips`: EXC_BREAKPOINT (SIGTRAP), "BUG IN CLIENT OF LIBMALLOC:
memory corruption of free block", faulting thread 3:

```
_xzm_xzone_malloc_freelist_outlined + 868
operator_new_impl[abi:nqe220106](unsigned long, std::__type_descriptor_t) + 36
$_0::operator()() const::'lambda0'(unsigned long, std::__type_descriptor_t)::__invoke(unsigned long, std::__type_descriptor_t) + 16
std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>::__grow_by_and_replace(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, char const*) + 108
std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>& std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>::__assign_no_alias<true>(char const*, unsigned long) + 72
Luau::ConstraintGenerator::resolveTableType(std::__1::shared_ptr<Luau::Scope> const&, Luau::AstType*, Luau::AstTypeTable*, bool, bool) + 1068
Luau::ConstraintGenerator::resolveType_(std::__1::shared_ptr<Luau::Scope> const&, Luau::AstType*, bool, bool) + 164
Luau::ConstraintGenerator::visit(std::__1::shared_ptr<Luau::Scope> const&, Luau::AstStatLocal*) + 592
Luau::ConstraintGenerator::visit(std::__1::shared_ptr<Luau::Scope> const&, Luau::AstStat*) + 664
Luau::ConstraintGenerator::visitBlockWithoutChildScope(std::__1::shared_ptr<Luau::Scope> const&, Luau::AstStatBlock*) + 124
```

At the same moment the main thread waits in `Luau::Frontend::checkQueuedModules` and another
worker is allocating in `Luau::DataFlowGraphBuilder::resolveCaptures()`.

`moo-analyze-2026-11-21-083433.ips` (the newest): SIGABRT, "abort() called", faulting thread 1
(the argument lists of the two `check` frames are shortened to `...` here):

```
__pthread_kill + 8
pthread_kill + 296
abort + 148
malloc_vreport + 900
malloc_report + 64
___BUG_IN_CLIENT_OF_LIBMALLOC_POINTER_BEING_FREED_WAS_NOT_ALLOCATED + 76
std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>::__grow_by_and_replace(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, char const*) + 236
std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>& std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>::__assign_no_alias<false>(char const*, unsigned long) + 112
Luau::check(...) + 960
Luau::Frontend::check(...) + 608
Luau::Frontend::checkBuildQueueItem(Luau::BuildQueueItem&) + 1188
Luau::Frontend::performQueueItemTask(std::__1::shared_ptr<Luau::BuildQueueWorkState>, unsigned long) + 56
```

Both crash while assigning a `std::string` on a worker thread during type checking, which points
at a string, or the memory around it, shared between worker threads without a lock. That is a
reading of two stack traces, not a finding.

## Workaround and who is exposed

- mas-bandwidth/serialize.moo passes `-j1` to the analyzer since PR #35 (merged 2026-11-21). This
  is a workaround, not a fix.
- Still on the default threads: mas-bandwidth/schema's moo leg (`make/moo*.mk` and
  `internal/codegen/moo`) and moo's own CI (`ci.yml` and `nightly.yml` run `build/moo-analyze`).
- moo's own CI is not a reliable alarm for this yet. PR #76 found that GitHub's `ubuntu-latest`
  moved to GCC 15.2 on 2026-11-10 and moo's nightly then stayed red for twelve days without anyone
  noticing, so an analyzer crash in the nightly would also have gone unseen. A test written to
  catch this bug has to fail somewhere that is watched.

## Not known yet

- Whether the bug is inherited from upstream Luau at moo's merge base, or was introduced by moo's
  changes to the analyzer builtins.
- Whether it happens on Linux or Windows: every measurement so far is macOS on Apple silicon.

## Done means

1. The cause is found and fixed in the analyzer.
2. A test runs the analyzer on its default threads enough times that it would have caught this
   (at the rate above, about 1 crash in 76 runs, so several hundred runs).
3. The `-j1` workarounds are removed, starting with serialize.moo's.
