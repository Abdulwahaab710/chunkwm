#include "event.h"

#define internal static

internal event_loop EventLoop = {};

/* NOTE(koekeishiya): Must be thread-safe! Called through ConstructEvent macro */
void AddEvent(chunk_event Event)
{
    if (EventLoop.Running && Event.Handle) {
        pthread_mutex_lock(&EventLoop.Lock);
        EventLoop.Queue.push(Event);
        pthread_mutex_unlock(&EventLoop.Lock);
        sem_post(EventLoop.Semaphore);
    }
}

internal void *
ProcessEventQueue(void *)
{
    while (EventLoop.Running) {
        pthread_mutex_lock(&EventLoop.Lock);
        bool HasWork = !EventLoop.Queue.empty();
        pthread_mutex_unlock(&EventLoop.Lock);

        while (HasWork) {
            pthread_mutex_lock(&EventLoop.Lock);
            chunk_event Event = EventLoop.Queue.front();
            EventLoop.Queue.pop();

            HasWork = !EventLoop.Queue.empty();
            pthread_mutex_unlock(&EventLoop.Lock);

            (*Event.Handle)(&Event);
        }

        int Result = sem_wait(EventLoop.Semaphore);
        if (Result) {
            uint64_t ID;
            pthread_threadid_np(NULL, &ID);
            fprintf(stderr, "%lld: sem_wait(..) failed\n", ID);
        }
    }

    return NULL;
}

/* NOTE(koekeishiya): Initialize required mutexes and semaphore for the eventloop */
internal bool
BeginEventLoop()
{
    bool Result = true;

    if ((EventLoop.Semaphore = sem_open("eventloop_semaphore", O_CREAT, 0644, 0)) == SEM_FAILED) {
        fprintf(stderr, "chunkwm: could not initialize semaphore!");
        goto sem_err;
    }

    if (pthread_mutex_init(&EventLoop.Lock, NULL) != 0) {
        fprintf(stderr, "chunkwm: could not initialize work mutex!");
        goto work_err;
    }

    goto out;

work_err:
    sem_destroy(EventLoop.Semaphore);

sem_err:
    Result = false;

out:
    return Result;
}

/* NOTE(koekeishiya): Destroy mutexes and condition used by the event-loop */
internal void
EndEventLoop()
{
    pthread_mutex_destroy(&EventLoop.Lock);
    sem_destroy(EventLoop.Semaphore);
}

void PauseEventLoop()
{
    // TODO(koekeishiya): Probably want a proper way to temporarily ignore all events
}

void ResumeEventLoop()
{
    // TODO(koekeishiya): NYI
}

bool StartEventLoop()
{
    if ((!EventLoop.Running) && (BeginEventLoop())) {
        EventLoop.Running = true;
        pthread_create(&EventLoop.Thread, NULL, &ProcessEventQueue, NULL);
        return true;
    }

    return false;
}

void StopEventLoop()
{
    if (EventLoop.Running) {
        EventLoop.Running = false;
        pthread_join(EventLoop.Thread, NULL);
        EndEventLoop();
    }
}
