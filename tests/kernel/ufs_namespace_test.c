/*
 * ILP32 contract for the private UFS namespace serialization gate. The
 * fixture links the production provider and controls its sleep and wakeup
 * boundary without substituting another lock implementation.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <sys/systm.h>

static int sleep_result;
static int release_during_sleep;
static unsigned sleep_calls;
static unsigned wakeup_calls;
static caddr_t sleep_channel;

int
tsleep(caddr_t channel, int priority, u_int timeout)
{
    sleep_calls++;
    sleep_channel = channel;
    HK_CHECK(priority == (PINOD | PCATCH));
    HK_CHECK(timeout == 0);
    if (release_during_sleep) {
        release_during_sleep = 0;
        ufs_namespace_exit();
    }
    return sleep_result;
}

void
wakeup(caddr_t channel)
{
    HK_CHECK(channel == sleep_channel);
    wakeup_calls++;
}

static void
reset_observations(void)
{
    sleep_result = 0;
    release_during_sleep = 0;
    sleep_calls = 0;
    wakeup_calls = 0;
    sleep_channel = NULL;
}

static void
free_acquisition_requires_no_sleep(void)
{
    reset_observations();
    HK_CHECK(ufs_namespace_enter() == 0);
    HK_CHECK(sleep_calls == 0);
    ufs_namespace_exit();
    HK_CHECK(wakeup_calls == 0);

    HK_CHECK(ufs_namespace_enter() == 0);
    HK_CHECK(sleep_calls == 0);
    ufs_namespace_exit();
    HK_CHECK(wakeup_calls == 0);
}

static void
waiter_acquires_after_owner_release(void)
{
    reset_observations();
    HK_CHECK(ufs_namespace_enter() == 0);
    release_during_sleep = 1;
    HK_CHECK(ufs_namespace_enter() == 0);
    HK_CHECK(sleep_calls == 1);
    HK_CHECK(wakeup_calls == 1);
    HK_CHECK(sleep_channel != NULL);
    ufs_namespace_exit();
    HK_CHECK(wakeup_calls == 1);
}

static void
interrupted_waiter_preserves_owner(int error)
{
    reset_observations();
    HK_CHECK(ufs_namespace_enter() == 0);
    sleep_result = error;
    HK_CHECK(ufs_namespace_enter() == error);
    HK_CHECK(sleep_calls == 1);
    HK_CHECK(wakeup_calls == 0);

    sleep_result = 0;
    release_during_sleep = 1;
    HK_CHECK(ufs_namespace_enter() == 0);
    HK_CHECK(sleep_calls == 2);
    HK_CHECK(wakeup_calls == 1);
    ufs_namespace_exit();
    HK_CHECK(wakeup_calls == 1);
}

static void
release_without_owner(void)
{
    ufs_namespace_exit();
}

int
main(void)
{
    free_acquisition_requires_no_sleep();
    waiter_acquires_after_owner_release();
    interrupted_waiter_preserves_owner(EINTR);
    interrupted_waiter_preserves_owner(ERESTART);
    HK_EXPECT_PANIC("ufs namespace gate", release_without_owner);
    return hk_verdict("UFS namespace serialization");
}
