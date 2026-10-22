------------------------------ MODULE Coroutines ------------------------------
\* moo: the coroutine states (docs/DESIGN.md "The rest of determinism", Coroutines; docs/DETERMINISM.md 21).
\*
\* Luau's coroutines are cooperative: one thread runs at a time, and a resume or a yield moves control by an explicit
\* operation. The state of each coroutine is what lua_costatus (VM/src/lapi.cpp) reports: "running" (it is the
\* current thread), "suspended" (created and not started, or yielded: co->status == LUA_YIELD, or no frames and a
\* function on the stack), "normal" (it resumed another coroutine and has frames), "dead" (finished, errored, or
\* closed). The main thread is not a coroutine: it is always running or normal.
\*
\* The resume chain is a stack: the main thread at the bottom, the running thread on top, every thread under it
\* "normal". coroutine.resume (lcorolib.cpp auxresume) runs a coroutine only when it is suspended and refuses any other
\* status by name ("cannot resume %s coroutine"); coroutine.yield (lua_yield, ldo.cpp) suspends the running coroutine
\* and returns control to the thread that resumed it; returning or raising an error makes it dead and returns to the
\* resumer; coroutine.close (coclose) accepts only a suspended or dead coroutine ("cannot close %s coroutine"
\* otherwise) and leaves it dead (lua_resetthread). Every step is chosen by the program, and its result is a function
\* of these states alone: that is the determinism argument for coroutines.
\*
\* Plants: Plant = "closenormal" lets close accept a normal coroutine; Plant = "resumenormal" lets resume accept a
\* normal one (a recursive resume). Each breaks an invariant below.
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS Cos, Plant \* Cos: the coroutines (model values); Plant: "none" (as built), "closenormal" or "resumenormal"

Main == "main"
Threads == Cos \cup {Main}
States == {"none", "suspended", "running", "normal", "dead"}

VARIABLES status, chain, refused

vars == <<status, chain, refused>>

Running == chain[Len(chain)]
Range(s) == {s[i] : i \in 1..Len(s)}

Init ==
    /\ status = [t \in Threads |-> IF t = Main THEN "running" ELSE "none"]
    /\ chain = <<Main>>
    /\ refused = ""

\* coroutine.create
Create(c) ==
    /\ status[c] = "none"
    /\ status' = [status EXCEPT ![c] = "suspended"]
    /\ refused' = ""
    /\ UNCHANGED chain

ResumeAccepts(c) == status[c] = "suspended" \/ (Plant = "resumenormal" /\ status[c] = "normal")

\* coroutine.resume by the running thread
Resume(c) ==
    /\ status[c] # "none"
    /\ IF ResumeAccepts(c)
       THEN /\ status' = [status EXCEPT ![Running] = "normal", ![c] = "running"]
            /\ chain' = Append(chain, c)
            /\ refused' = ""
       ELSE /\ refused' = "cannot resume " \o status[c] \o " coroutine"
            /\ UNCHANGED <<status, chain>>

\* coroutine.yield, return and error by the running coroutine (the main thread does none of these here)
Leave(to) ==
    /\ Running # Main
    /\ LET r == Running
           back == chain[Len(chain) - 1]
       IN /\ status' = [status EXCEPT ![r] = to, ![back] = "running"]
          /\ chain' = SubSeq(chain, 1, Len(chain) - 1)
          /\ refused' = ""

Yield == Leave("suspended")
Finish == Leave("dead") \* a return or an error: both are dead to lua_costatus

CloseAccepts(c) == status[c] \in {"suspended", "dead"} \/ (Plant = "closenormal" /\ status[c] = "normal")

\* coroutine.close by the running thread
Close(c) ==
    /\ status[c] # "none"
    /\ IF CloseAccepts(c)
       THEN /\ status' = [status EXCEPT ![c] = "dead"]
            /\ refused' = ""
            /\ UNCHANGED chain
       ELSE /\ refused' = "cannot close " \o status[c] \o " coroutine"
            /\ UNCHANGED <<status, chain>>

Next ==
    \/ \E c \in Cos : Create(c) \/ Resume(c) \/ Close(c)
    \/ Yield
    \/ Finish

Spec == Init /\ [][Next]_vars

TypeOK ==
    /\ status \in [Threads -> States]
    /\ status[Main] \in {"running", "normal"}
    /\ chain[1] = Main

\* exactly one thread runs, and it is the top of the chain
OneRunning == {t \in Threads : status[t] = "running"} = {Running}

\* the threads under the top of the chain are exactly the normal ones
NormalIsTheChain == {t \in Threads : status[t] = "normal"} = Range(SubSeq(chain, 1, Len(chain) - 1))

\* no thread is on the chain twice: a coroutine never resumes itself or one of its resumers
ChainDistinct == Cardinality(Range(chain)) = Len(chain)

\* a refusal names the status lua_costatus reports, which is never "suspended" for resume or "suspended"/"dead" for close
RefusalsNamed ==
    refused \in {""} \cup {"cannot resume " \o s \o " coroutine" : s \in {"running", "normal", "dead"}}
                     \cup {"cannot close " \o s \o " coroutine" : s \in {"running", "normal"}}

\* dead is final
DeadIsFinal == [][\A c \in Cos : status[c] = "dead" => status'[c] = "dead"]_vars
================================================================================
