---------------------------- MODULE DestructorQueue ----------------------------
\* moo: the userdata destructor queue (docs/DETERMINISM.md, 13; VM/src/lgc.cpp).
\*
\* Luau 0.741 runs a userdata's destructor inside the sweep, at the moment its block is
\* reached, so destructors run in GC page order and interleave with the sweep's progress.
\* moo's sweep only queues a dead userdata that has a destructor; at the end of the GC step
\* that queued it, the queue runs in creation-number order and each block is freed after its
\* destructor. Nothing else in the GC changes: this models only what the queue adds.
\*
\* An object's id is its creation number: ids are allocated in increasing order, so an id
\* order is the creation order. Page placement is chosen by the allocator and is independent
\* of the id. Every object has a destructor (objects without one are freed in the sweep, as
\* before, and never reach the queue).
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
    N,              \* objects that may ever be allocated (ids 1..N)
    Pages,          \* number of GC pages
    DrainOrder,     \* "creation" (as built) or "sweep" (the plant: page order, Luau's order)
    DrainWhen       \* "everystep" (as built) or "cycleend" (the plant: only when the sweep ends)

VARIABLES
    nextid,     \* the next creation number
    alloc,      \* allocated, not yet freed
    reach,      \* reachable from the roots
    page,       \* page of each allocated object
    phase,      \* "pause" or "sweep"
    doomed,     \* unreachable at the end of mark (atomic): the objects this cycle collects
    swept,      \* pages already swept this cycle
    queue,      \* dead userdata queued by the sweep, destructor not yet run
    instep,     \* TRUE between a sweep step and the end of that GC step
    ran,        \* every destructor run, in order
    batch       \* the destructors run by the last drain, in order

vars == <<nextid, alloc, reach, page, phase, doomed, swept, queue, instep, ran, batch>>

Ids == 1..N
PageIds == 1..Pages

Range(s) == {s[i] : i \in DOMAIN s}

Min(S) == CHOOSE x \in S : \A y \in S : x <= y

\* S as a sequence in increasing order
RECURSIVE Sorted(_)
Sorted(S) == IF S = {} THEN <<>> ELSE <<Min(S)>> \o Sorted(S \ {Min(S)})

\* the plant: sweep order, page by page, and inside a page the reverse of creation order
\* (inside a page the slot order is the allocator's free list, not the creation order)
RECURSIVE SweepOrder(_)
SweepOrder(S) ==
    IF S = {} THEN <<>>
    ELSE LET p == Min({page[x] : x \in S})
             inp == {x \in S : page[x] = p}
             last == CHOOSE x \in inp : \A y \in inp : x >= y
         IN <<last>> \o SweepOrder(S \ {last})

Init ==
    /\ nextid = 1
    /\ alloc = {}
    /\ reach = {}
    /\ page = [x \in {} |-> 1]
    /\ phase = "pause"
    /\ doomed = {}
    /\ swept = {}
    /\ queue = {}
    /\ instep = FALSE
    /\ ran = <<>>
    /\ batch = <<>>

\* ---- the program (runs only between GC steps) ----

Allocate(p) ==
    /\ ~instep
    /\ nextid <= N
    /\ alloc' = alloc \cup {nextid}
    /\ reach' = reach \cup {nextid}
    /\ page' = [x \in DOMAIN page \cup {nextid} |-> IF x = nextid THEN p ELSE page[x]]
    /\ nextid' = nextid + 1
    /\ UNCHANGED <<phase, doomed, swept, queue, instep, ran, batch>>

Drop(x) ==
    /\ ~instep
    /\ x \in reach
    /\ reach' = reach \ {x}
    /\ UNCHANGED <<nextid, alloc, page, phase, doomed, swept, queue, instep, ran, batch>>

\* ---- the GC ----

\* mark, as one step: the cycle collects what is allocated and unreachable now
MarkAndAtomic ==
    /\ ~instep
    /\ phase = "pause"
    /\ phase' = "sweep"
    /\ doomed' = alloc \ reach
    /\ swept' = {}
    /\ UNCHANGED <<nextid, alloc, reach, page, queue, instep, ran, batch>>

\* one GC step sweeps the next k pages in page order and queues their dead userdata
SweepStep(k) ==
    /\ ~instep
    /\ phase = "sweep"
    /\ swept /= PageIds
    /\ LET todo == {p \in PageIds : p \notin swept}
           first == Min(todo)
           now == {p \in todo : p < first + k}
       IN /\ swept' = swept \cup now
          /\ queue' = queue \cup {x \in doomed : x \in alloc /\ page[x] \in now}
    /\ instep' = TRUE
    /\ UNCHANGED <<nextid, alloc, reach, page, phase, doomed, ran, batch>>

\* the end of the GC step: run the queue, free each block, end the cycle when all pages are swept
EndStep ==
    /\ instep
    /\ swept' = swept
    /\ LET drain == DrainWhen = "everystep" \/ swept = PageIds
           order == IF DrainOrder = "creation" THEN Sorted(queue) ELSE SweepOrder(queue)
           left == IF drain THEN {} ELSE queue
       IN /\ ran' = IF drain THEN ran \o order ELSE ran
          /\ batch' = IF drain THEN order ELSE batch
          /\ alloc' = IF drain THEN alloc \ queue ELSE alloc
          /\ queue' = left
          /\ IF swept = PageIds /\ left = {}
             THEN phase' = "pause" /\ doomed' = {}
             ELSE UNCHANGED <<phase, doomed>>
    /\ instep' = FALSE
    /\ UNCHANGED <<nextid, reach, page>>

Next ==
    \/ \E p \in PageIds : Allocate(p)
    \/ \E x \in Ids : Drop(x)
    \/ MarkAndAtomic
    \/ \E k \in 1..Pages : SweepStep(k)
    \/ EndStep

Fairness ==
    /\ WF_vars(MarkAndAtomic)
    /\ WF_vars(\E k \in 1..Pages : SweepStep(k))
    /\ WF_vars(EndStep)

Spec == Init /\ [][Next]_vars /\ Fairness

\* ---- properties ----

TypeOK ==
    /\ nextid \in 1..(N + 1)
    /\ alloc \subseteq Ids /\ reach \subseteq alloc
    /\ phase \in {"pause", "sweep"}
    /\ doomed \subseteq Ids /\ queue \subseteq doomed
    /\ swept \subseteq PageIds
    /\ instep \in BOOLEAN

\* the program never sees a queued object, and the queue never outlives the GC step
QueueEmptyBetweenSteps == ~instep => queue = {}

\* a destructor runs only for an object that was unreachable when its cycle's mark ended
NeverDestructsReachable == \A i \in DOMAIN ran : ran[i] \notin reach

\* each destructor runs once
DestructsOnce == \A i, j \in DOMAIN ran : i /= j => ran[i] /= ran[j]

\* each drain runs in creation order
BatchInCreationOrder == \A i, j \in DOMAIN batch : i < j => batch[i] < batch[j]

\* every object dropped before a mark is destructed
EveryDeadObjectIsDestructed == \A x \in Ids : (x \in doomed) ~> (x \in Range(ran))

=============================================================================
