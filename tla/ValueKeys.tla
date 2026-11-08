------------------------------- MODULE ValueKeys -------------------------------
\* moo: value-keyed objects as table keys (docs/DESIGN.md "Layer 3c as built"; VM/src/ltable.cpp, lobject.cpp,
\* lgc.cpp). vec3, quat, transform and the layer 3c kinds vec2, mat3, plane, aabb and fix30 are GC objects that
\* compare by their bits: two objects of equal bits are one key. The GC turns the key of an entry whose value is nil
\* into LUA_TDEADKEY (removeentry) before the sweep frees anything, and a dead key keeps only the object's identity:
\* next() continues from a dead key only when given the very same object, and never reads the object, which the
\* sweep may already have freed.
\*
\* The model: objects 1..N (an id is a creation number: never reused), each with a value from Vals (its bits); a
\* table of S slots, each empty, a key (an object and whether its value is non-nil) or dead (an object, identity
\* only); the program's references; the heap. Collect is a whole GC cycle: clear, mark, sweep. Next(o) is next(t, o).
\* Every equality the table performs is recorded: comparing by value reads the key object, and a read of a freed
\* object sets freedread.
EXTENDS Naturals, FiniteSets

CONSTANTS
    N,      \* objects that may ever be allocated
    Vals,   \* the bit patterns an object may carry
    S,      \* table slots
    Plant   \* "none" (as built), "sweepfirst" (the sweep runs before removeentry) or "deadbyvalue" (a dead key
            \* compares by value)

VARIABLES
    nextid,     \* the next creation number
    val,        \* the bits of each allocated object
    heap,       \* allocated, not freed
    reach,      \* referenced by the program (the stack)
    slot,       \* the table: slot -> [kind |-> "empty"|"key"|"dead", obj |-> id, live |-> value non-nil]
    freedread,  \* an equality read a freed object
    lastnext    \* the next() of the last step, if it was one: [obj, result] with result "ok" or "raise"

vars == <<nextid, val, heap, reach, slot, freedread, lastnext>>

Ids == 1..N
Slots == 1..S
Empty == [kind |-> "empty", obj |-> 0, live |-> FALSE]
NoNext == [obj |-> 0, result |-> "none"]

\* the table's key equality for a live key (luaO_rawequalKey): by value, which reads the key object
KeyEq(s, o) == slot[s].kind = "key" /\ val[slot[s].obj] = val[o]
\* a dead key: identity only (as built), or by value (the plant)
DeadEq(s, o) == slot[s].kind = "dead" /\ IF Plant = "deadbyvalue" THEN val[slot[s].obj] = val[o] ELSE slot[s].obj = o
\* the objects an equality against o reads: every live key compared, and every dead key under the plant
Reads(o) == {slot[s].obj : s \in {x \in Slots : slot[x].kind = "key" \/ (Plant = "deadbyvalue" /\ slot[x].kind = "dead")}}

Init ==
    /\ nextid = 1
    /\ val = [x \in Ids |-> CHOOSE v \in Vals : TRUE]
    /\ heap = {}
    /\ reach = {}
    /\ slot = [s \in Slots |-> Empty]
    /\ freedread = FALSE
    /\ lastnext = NoNext

Alloc(v) ==
    /\ nextid <= N
    /\ val' = [val EXCEPT ![nextid] = v]
    /\ heap' = heap \cup {nextid}
    /\ reach' = reach \cup {nextid}
    /\ nextid' = nextid + 1
    /\ lastnext' = NoNext
    /\ UNCHANGED <<slot, freedread>>

Drop(o) ==
    /\ o \in reach
    /\ reach' = reach \ {o}
    /\ lastnext' = NoNext
    /\ UNCHANGED <<nextid, val, heap, slot, freedread>>

\* t[o] = non-nil: an equal key takes the value; otherwise the key goes into an empty slot, after a rehash if none is
\* empty (a rehash drops dead keys); a full table of live keys refuses (the model's bound, not the VM's)
Insert(o) ==
    /\ o \in reach
    /\ freedread' = (freedread \/ ~(Reads(o) \subseteq heap))
    /\ IF \E s \in Slots : KeyEq(s, o)
       THEN LET s == CHOOSE x \in Slots : KeyEq(x, o) IN slot' = [slot EXCEPT ![s].live = TRUE]
       ELSE LET t == IF \E x \in Slots : slot[x].kind = "empty" THEN slot
                     ELSE [x \in Slots |-> IF slot[x].kind = "dead" THEN Empty ELSE slot[x]]
            IN /\ \E x \in Slots : t[x].kind = "empty"
               /\ LET s == CHOOSE x \in Slots : t[x].kind = "empty"
                  IN slot' = [t EXCEPT ![s] = [kind |-> "key", obj |-> o, live |-> TRUE]]
    /\ lastnext' = NoNext
    /\ UNCHANGED <<nextid, val, heap, reach>>

\* t[o] = nil: the equal key's value becomes nil; the key stays until the GC removes the entry
Remove(o) ==
    /\ o \in reach
    /\ freedread' = (freedread \/ ~(Reads(o) \subseteq heap))
    /\ \E s \in Slots : KeyEq(s, o)
    /\ LET s == CHOOSE x \in Slots : KeyEq(x, o) IN slot' = [slot EXCEPT ![s].live = FALSE]
    /\ lastnext' = NoNext
    /\ UNCHANGED <<nextid, val, heap, reach>>

\* one GC cycle: an entry with a nil value loses its key (removeentry: the key becomes dead); the marks are the
\* program's references and the keys of entries with a value; the sweep frees the rest. The plant sweeps first, so a
\* key whose value is nil still names an object it may have freed.
Cleared == [s \in Slots |-> IF slot[s].kind = "key" /\ ~slot[s].live THEN [slot[s] EXCEPT !.kind = "dead"] ELSE slot[s]]
Collect ==
    LET marked == reach \cup {slot[s].obj : s \in {x \in Slots : slot[x].kind = "key" /\ slot[x].live}}
    IN /\ heap' = heap \cap marked
       /\ slot' = IF Plant = "sweepfirst" THEN slot ELSE Cleared
       /\ lastnext' = NoNext
       /\ UNCHANGED <<nextid, val, reach, freedread>>

\* next(t, o): o must be the key of a slot (a live key by value, a dead key by identity), else "invalid key to 'next'"
Next(o) ==
    /\ o \in reach
    /\ freedread' = (freedread \/ ~(Reads(o) \subseteq heap))
    /\ lastnext' = [obj |-> o, result |-> IF \E s \in Slots : KeyEq(s, o) \/ DeadEq(s, o) THEN "ok" ELSE "raise"]
    /\ UNCHANGED <<nextid, val, heap, reach, slot>>

Step ==
    \/ \E v \in Vals : Alloc(v)
    \/ \E o \in Ids : Drop(o) \/ Insert(o) \/ Remove(o) \/ Next(o)
    \/ Collect

Spec == Init /\ [][Step]_vars

\* ---- invariants ----

TypeOK ==
    /\ heap \subseteq Ids /\ reach \subseteq heap
    /\ \A s \in Slots : slot[s].kind \in {"empty", "key", "dead"}

\* no equality ever reads a freed object
NoFreedRead == ~freedread

\* a key that can still be compared by value names an allocated object
LiveKeysAllocated == \A s \in Slots : slot[s].kind = "key" => slot[s].obj \in heap

\* equal bits are one key: no two key slots hold equal values
OneSlotPerValue == \A s, u \in Slots : (s /= u /\ slot[s].kind = "key" /\ slot[u].kind = "key") => val[slot[s].obj] /= val[slot[u].obj]

\* next() from a dead key continues only with the same object; an equal, different object raises
DeadKeyIdentity ==
    lastnext.result = "ok" =>
        \E s \in Slots : (slot[s].kind = "key" /\ val[slot[s].obj] = val[lastnext.obj]) \/ (slot[s].kind = "dead" /\ slot[s].obj = lastnext.obj)

=============================================================================
