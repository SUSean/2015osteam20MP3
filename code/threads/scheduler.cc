// scheduler.cc
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would
//	end up calling FindNextToRun(), and that would put us in an
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{
    readySJFList = new SortedList<Thread*>(compare_by_bursttime);
    readyPriorityList = new SortedList<Thread*>(compare_by_priority);
    readyRRList = new List<Thread*>;
    toBeDestroyed = NULL;
    intHandler = new SchedulerIntHandler();
}

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{
    delete readyPriorityList;
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    if(thread->getPriority()<50){
        readyRRList->Append(thread);
        cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is inserted into queue L3\n";
    }
    else if(thread->getPriority()>=50&&thread->getPriority()<100){
        readyPriorityList->Insert(thread);
        cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is inserted into queue L2\n";

        if(thread->getPriority()>kernel->currentThread->getPriority()){
            intHandler->Schedule(10);
        }
    }
    else if(thread->getPriority()>=100&&thread->getPriority()<150){
        readySJFList->Insert(thread);
        cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is inserted into queue L1\n";

        if(thread->getBurstTime()<kernel->currentThread->getBurstTime()-(kernel->stats->totalTicks-kernel->currentThread->getStartTime())){
            intHandler->Schedule(10);
            kernel->currentThread->setBurstTime(kernel->currentThread->getBurstTime()-
                                                (kernel->stats->totalTicks-kernel->currentThread->getStartTime()));
            if(kernel->currentThread->getBurstTime()<0)
                kernel->currentThread->setBurstTime(0);
        }
    }

    thread->setReadyTime(kernel->stats->totalTicks);

}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    Thread * newThread;
    aging(readySJFList);
    aging(readyPriorityList);
    aging(readyRRList);
    if(readySJFList->IsEmpty()){
        if(readyPriorityList->IsEmpty()){
            if(readyRRList->IsEmpty())
                return NULL;
            else{
                newThread = readyRRList->RemoveFront();
                cout << "Tick " << kernel->stats->totalTicks << ": Thread " << newThread->getID() << " is removed from queue L3\n";
                return newThread;
            }
        }
        else {
            newThread=readyPriorityList->RemoveFront();
            cout << "Tick " << kernel->stats->totalTicks << ": Thread " << newThread->getID() << " is removed from queue L2\n";
            return newThread;
        }
    }
    else{
        newThread=readySJFList->RemoveFront();
        cout << "Tick " << kernel->stats->totalTicks << ": Thread " << newThread->getID() << " is removed from queue L1\n";
        return newThread;
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running

    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());

    // This is a machine-dependent assembly language routine defined
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    cout << "Tick " << kernel->stats->totalTicks << ": Thread " << nextThread->getID() << " is now selected for execution\n";
    cout << "Tick " << kernel->stats->totalTicks << ": Thread " << oldThread->getID() << " is replaced, and it has executed "
            <<oldThread->getStopTime()-oldThread->getStartTime()<< " ticks\n";
    nextThread->setStartTime(kernel->stats->totalTicks);
    SWITCH(oldThread, nextThread);

    // we're back, running oldThread

    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());
    //cout << "Tick " << kernel->stats->totalTicks << ": Thread " << oldThread->getName() << " is now selected for execution\n";
    //cout << "Tick " << kernel->stats->totalTicks << ": Thread " << nextThread->getName() << " is replaced\n";
    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up

    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyPriorityList->Apply(ThreadPrint);
}

void
Scheduler::aging(List<Thread *> *list)
{
    ListIterator<Thread *> *iter= new ListIterator<Thread *>((List<Thread *> *)list);
    for (; !iter->IsDone(); iter->Next()){
	    Thread* thread=iter->Item();
	    if(kernel->stats->totalTicks-thread->getReadyTime()>=1500){
            thread->setReadyTime(kernel->stats->totalTicks);
            thread->aging();
            list->Remove(thread);
            cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " changes its priority from "
                << thread->getPriority()-10 << " to " << thread->getPriority() << "\n";
            if(thread->getPriority()>=50 && thread->getPriority()<100 && list!=readyPriorityList){
                cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is removed from queue L3\n";
                readyPriorityList->Insert(thread);
                cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is inserted into queue L2\n";
            }
            else if(thread->getPriority()>=100 && thread->getPriority()<150 && list!=readySJFList){
                cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is removed from queue L2\n";
                readyPriorityList->Insert(thread);
                cout << "Tick " << kernel->stats->totalTicks << ": Thread " << thread->getID() << " is inserted into queue L1\n";
            }
            else
                list->Append(thread);
        }
    }
}



int
Scheduler::compare_by_priority(Thread* t1,Thread* t2)
{
    if(t1->getPriority()<t2->getPriority())
        return 1;
    else if(t1->getPriority()>t2->getPriority())
        return -1;
    else{
        if(t1->getID()<t2->getID()){
            return -1;
        }
        else if(t1->getID()>t2->getID()){
            return 1;
        }
        else
            return 0;
    }
}
int
Scheduler::compare_by_bursttime(Thread* t1,Thread* t2)
{
    if(t1->getBurstTime()<t2->getBurstTime())
        return -1;
    else if(t1->getBurstTime()>t2->getBurstTime())
        return 1;
    else{
        if(t1->getID()<t2->getID()){
            return -1;
        }
        else if(t1->getID()>t2->getID()){
            return 1;
        }
        else
            return 0;
    }
}
void
SchedulerIntHandler::CallBack()
{
    kernel->interrupt->YieldOnReturn();
    cout << "Tick " << kernel->stats->totalTicks << ": Thread " << kernel->currentThread->getID() << " is preempted\n";
}
void
SchedulerIntHandler::Schedule(int time)
{
    kernel->interrupt->Schedule(this, time, TimerInt);
}
