------------------------------ MODULE TempReuse ------------------------------
\* moo: expression-temporary reuse for vec3, quat and transform (docs/DESIGN.md "Expression-temporary reuse";
\* Compiler/src/MooTempReuse.cpp, VM/src/moo_tempreuse.cpp).
\*
\* The compiler marks an arithmetic op (ADDTB/ADDTC, SUBTB/SUBTC, MULTB/MULTC, MULKTB, MINUST) when one operand
\* register is an expression temporary that an arithmetic op of the same expression produced: the register was
\* allocated for that operand, written only by its producer and read only by this op. The VM then overwrites the
\* object in that register in place, instead of allocating, when it is a vec3/quat/transform of the result's type
\* and its creation number is at or above the fence (global_State::mootempfence).
\*
\* The compiler's guarantee is the shape of the actions below: a temporary is written only by Produce/ProduceTM/
\* Consume and read only by its one Consume. What can still make the object in a temporary visible to the program
\* is code that runs while it is live: a metamethod (which may be handed the object as an operand, or return an
\* object the program holds), or a host callback that reads the running frame's registers (an interrupt, a debug
\* hook, a userdata destructor). Each of those moves the fence to the next creation number when it returns, so every
\* object that exists at that point is never reused in place.
\*
\* Objects are named by their creation number (gcseq), 1, 2, ...; 0 in a register means it holds no object.
\*
\* Plants: Plant = "notmfence" leaves the fence where it is after a metamethod; "nohostfence" after a host callback;
\* "nocheck" reuses without comparing the creation number with the fence. Each breaks NoVisibleMutation.
EXTENDS Integers, FiniteSets

CONSTANTS Temps, MaxSeq, Plant \* Temps: temporary registers; MaxSeq: creation numbers 1..MaxSeq; Plant: "none" (as built)

VARIABLES seq, fence, reg, visible, bad

vars == <<seq, fence, reg, visible, bad>>

Objects == 1..MaxSeq
InTemps == {reg[t] : t \in Temps} \ {0}

TypeOK ==
    /\ seq \in 1..(MaxSeq + 1)
    /\ fence \in 1..(MaxSeq + 1)
    /\ reg \in [Temps -> 0..MaxSeq]
    /\ visible \subseteq Objects
    /\ bad \in BOOLEAN

Init ==
    /\ seq = 1
    /\ fence = 1
    /\ reg = [t \in Temps |-> 0]
    /\ visible = {}
    /\ bad = FALSE

\* the program makes an object of its own (a constructor, a library call) and holds it: visible from its birth
ProgramAlloc ==
    /\ seq <= MaxSeq
    /\ visible' = visible \cup {seq}
    /\ seq' = seq + 1
    /\ UNCHANGED <<fence, reg, bad>>

\* a metamethod returns into temporary t, with the registers before the call in r0. It may make one new object and
\* keep it, and it returns an object it can reach: one the program holds, one it was handed (escaped), or the new one.
\* Temporaries are out of its reach.
TMReturn(r0, t, escaped) ==
    \E new \in BOOLEAN, keep \in BOOLEAN :
        /\ (new => seq <= MaxSeq)
        /\ LET s2      == IF new THEN seq + 1 ELSE seq
               created == IF new THEN {seq} ELSE {}
               held    == visible \cup escaped \cup (IF keep THEN created ELSE {})
           IN \E r \in held \cup created :
                /\ visible' = held
                /\ reg' = [r0 EXCEPT ![t] = r]
                /\ seq' = s2
                /\ fence' = IF Plant = "notmfence" THEN fence ELSE s2

\* the compiler writes a temporary only when it is dead (a temporary belongs to one operand of one op)
DeadOrSame(s, t) == IF t = 0 THEN TRUE ELSE (t = s \/ reg[t] = 0)

\* an arithmetic op produces into a dead temporary, on the fast path: a new object nothing else holds
Produce(t) ==
    /\ reg[t] = 0
    /\ seq <= MaxSeq
    /\ reg' = [reg EXCEPT ![t] = seq]
    /\ seq' = seq + 1
    /\ UNCHANGED <<fence, visible, bad>>

\* an arithmetic op produces into a dead temporary through a metamethod (an operand is a table or userdata)
ProduceTM(t) ==
    /\ reg[t] = 0
    /\ TMReturn(reg, t, {})
    /\ UNCHANGED bad

Reusable(o) == o # 0 /\ (Plant = "nocheck" \/ o >= fence)

\* the marked op consumes the temporary in s; its result goes to temporary t (t may be s) or, with t = 0, to where
\* the program sees it (a local, a field, an argument, a return). Fast path in place: the object is overwritten
\* and becomes the result.
ConsumeInPlace(s, t) ==
    /\ reg[s] # 0
    /\ DeadOrSame(s, t)
    /\ Reusable(reg[s])
    /\ bad' = (bad \/ reg[s] \in visible)
    /\ IF t = 0
       THEN /\ reg' = [reg EXCEPT ![s] = 0]
            /\ visible' = visible \cup {reg[s]}
       ELSE /\ reg' = [[reg EXCEPT ![s] = 0] EXCEPT ![t] = reg[s]]
            /\ UNCHANGED visible
    /\ UNCHANGED <<seq, fence>>

\* fast path without reuse (the object is below the fence, or not of the result's type): a new object
ConsumeAlloc(s, t) ==
    /\ reg[s] # 0
    /\ DeadOrSame(s, t)
    /\ seq <= MaxSeq
    /\ IF t = 0
       THEN /\ reg' = [reg EXCEPT ![s] = 0]
            /\ visible' = visible \cup {seq}
       ELSE /\ reg' = [[reg EXCEPT ![s] = 0] EXCEPT ![t] = seq]
            /\ UNCHANGED visible
    /\ seq' = seq + 1
    /\ UNCHANGED <<fence, bad>>

\* the other operand has a metamethod: the temporary's object is handed to it, and the result is what it returns
ConsumeTM(s, t) ==
    /\ reg[s] # 0
    /\ t # 0
    /\ DeadOrSame(s, t)
    /\ TMReturn([reg EXCEPT ![s] = 0], t, {reg[s]})
    /\ UNCHANGED bad

\* an unmarked use: the temporary is an argument, a stored value, a returned value, or the operand of an op the
\* compiler did not mark; the program may hold its object from then on, and the temporary is dead
Use(s) ==
    /\ reg[s] # 0
    /\ visible' = visible \cup {reg[s]}
    /\ reg' = [reg EXCEPT ![s] = 0]
    /\ UNCHANGED <<seq, fence, bad>>

\* a host callback runs mid-frame (interrupt, debug hook, destructor): it may read any register and keep what it
\* reads, or write a register with an object it holds
Host ==
    /\ \E S \in SUBSET Temps :
          visible' = visible \cup ({reg[t] : t \in S} \ {0})
    /\ \E w \in Temps \cup {0}, o \in visible \cup {0} :
          reg' = IF w = 0 \/ o = 0 THEN reg ELSE [reg EXCEPT ![w] = o]
    /\ fence' = IF Plant = "nohostfence" THEN fence ELSE seq
    /\ UNCHANGED <<seq, bad>>

Next ==
    \/ ProgramAlloc
    \/ Host
    \/ \E t \in Temps : Produce(t) \/ ProduceTM(t)
    \/ \E s \in Temps : Use(s)
    \/ \E s \in Temps, t \in Temps \cup {0} : ConsumeInPlace(s, t) \/ ConsumeAlloc(s, t) \/ ConsumeTM(s, t)

Spec == Init /\ [][Next]_vars

\* the guarantee: the VM never overwrites an object the program can see
NoVisibleMutation == ~bad

\* the inductive core: an object in a temporary at or above the fence is invisible, and each temporary holds its own
FreshAboveFenceInvisible == \A t \in Temps : (reg[t] # 0 /\ reg[t] >= fence) => reg[t] \notin visible

TempsDistinct == \A s, t \in Temps : (s # t /\ reg[s] # 0 /\ reg[s] >= fence) => reg[s] # reg[t]

=============================================================================
