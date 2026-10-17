------------------------------ MODULE NumericFor ------------------------------
\* moo: the numeric for loop (docs/DESIGN.md "Exact integers"; VM/src/lvmexecute.cpp LOP_FORNPREP/LOP_FORNLOOP).
\*
\* Numbers are raw integers in the domain [-M, M] (M stands for INT64_MAX). `for i = from, to, step` runs once for
\* each k >= 0 with from + k*step between from and to, inclusive. moo computes that count exactly before the loop
\* starts and then counts it down, so the index never leaves [min(from, to), max(from, to)]: no add can saturate,
\* and the loop ends after exactly that many iterations, for every from, to and step in the domain.
\*
\* The plant is Luau's scheme on a saturating add (layer 3's temporary bridge): add the step, test the bound,
\* and stop when the index can no longer move. Near the edge of the domain it runs one iteration too many.
EXTENDS Integers

CONSTANTS M, Scheme \* Scheme: "count" (as built) or "saturate" (the plant)

VARIABLES from, to, step, idx, left, runs, phase

vars == <<from, to, step, idx, left, runs, phase>>

Dom == -M..M
Sat(x) == IF x > M THEN M ELSE IF x < -M THEN -M ELSE x

\* the defined number of iterations
Trips(f, t, s) ==
    IF s > 0 THEN (IF f <= t THEN (t - f) \div s + 1 ELSE 0)
             ELSE (IF f >= t THEN (f - t) \div (-s) + 1 ELSE 0)

InBound(i) == IF step > 0 THEN i <= to ELSE to <= i

Init ==
    /\ from \in Dom /\ to \in Dom /\ step \in Dom \ {0}
    /\ idx = from /\ left = 0 /\ runs = 0 /\ phase = "prep"

Prep ==
    /\ phase = "prep"
    /\ IF Scheme = "count"
       THEN LET n == Trips(from, to, step)
            IN /\ left' = IF n = 0 THEN 0 ELSE n - 1
               /\ phase' = IF n = 0 THEN "done" ELSE "body"
       ELSE /\ left' = 0
            /\ phase' = IF InBound(from) THEN "body" ELSE "done"
    /\ UNCHANGED <<from, to, step, idx, runs>>

Body ==
    /\ phase = "body"
    /\ runs' = runs + 1
    /\ phase' = "loop"
    /\ UNCHANGED <<from, to, step, idx, left>>

Loop ==
    /\ phase = "loop"
    /\ IF Scheme = "count"
       THEN IF left = 0
            THEN phase' = "done" /\ UNCHANGED <<idx, left>>
            ELSE /\ idx' = idx + step \* exact: never leaves the domain
                 /\ left' = left - 1
                 /\ phase' = "body"
       ELSE LET n == Sat(idx + step)
            IN /\ idx' = n
               /\ phase' = IF InBound(n) /\ n # idx THEN "body" ELSE "done"
               /\ UNCHANGED left
    /\ UNCHANGED <<from, to, step, runs>>

Done == phase = "done" /\ UNCHANGED vars

Next == Prep \/ Body \/ Loop \/ Done

Spec == Init /\ [][Next]_vars /\ WF_vars(Prep \/ Body \/ Loop)

\* the index never leaves the domain, nor the range between from and to
IndexInRange == idx \in Dom /\ (IF step > 0 THEN from <= idx /\ (runs = 0 \/ idx <= to) ELSE idx <= from /\ (runs = 0 \/ to <= idx))

\* never more iterations than defined, and exactly that many at the end
NeverTooMany == runs <= Trips(from, to, step)
ExactAtEnd == phase = "done" => runs = Trips(from, to, step)

Terminates == <>(phase = "done")
=============================================================================
