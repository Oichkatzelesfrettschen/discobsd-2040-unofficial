/*
 * System calls related to processes and protection.
 *
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>

void
getpid(void)
{
    u.u_rval = u.u_procp->p_pid;
}

void
getppid(void)
{
    u.u_rval = u.u_procp->p_ppid;
}

void
getpgrp(void)
{
    u.u_rval = u.u_procp->p_pgrp;
}

void
getuid(void)
{
    u.u_rval = u.u_ruid;
}

void
geteuid(void)
{
    u.u_rval = u.u_uid;
}

void
getgid(void)
{
    u.u_rval = u.u_rgid;
}

void
getegid(void)
{
    u.u_rval = u.u_groups[0];
}

/*
 * The user structure and public system-call interface both use gid_t
 * entries.  The count occupies one unsigned system-call word.
 */
void
getgroups(void)
{
    struct getgroups_args {
        u_int   gidsetsize;
        gid_t *gidset;
    } *arguments = (struct getgroups_args *)u.u_arg;
    gid_t *group_pointer;

    for (group_pointer = &u.u_groups[NGROUPS];
        group_pointer > u.u_groups; group_pointer--)
        if (group_pointer[-1] != NOGROUP)
            break;
    if (arguments->gidsetsize <
        (u_int)(group_pointer - u.u_groups)) {
        u.u_error = EINVAL;
        return;
    }
    arguments->gidsetsize = group_pointer - u.u_groups;
    u.u_error = copyout((caddr_t)u.u_groups,
        (caddr_t)arguments->gidset,
        arguments->gidsetsize * sizeof(u.u_groups[0]));
    if (u.u_error)
        return;
    u.u_rval = arguments->gidsetsize;
}

void
setpgrp(void)
{
    struct proc *process;
    struct setpgrp_args {
        int pid;
        int pgrp;
    } *arguments = (struct setpgrp_args *)u.u_arg;

    if (!PGRP_VALID(arguments->pgrp)) {
        u.u_error = EINVAL;
        return;
    }
    if (arguments->pid == 0)
        arguments->pid = u.u_procp->p_pid;
    process = pfind(arguments->pid);
    if (process == 0) {
        u.u_error = ESRCH;
        return;
    }
    if (process->p_uid != u.u_uid && u.u_uid && !inferior(process)) {
        u.u_error = EPERM;
        return;
    }
    process->p_pgrp = arguments->pgrp;
}

void
setgroups(void)
{
    struct setgroups_args {
        u_int   gidsetsize;
        gid_t *gidset;
    } *arguments = (struct setgroups_args *)u.u_arg;
    gid_t *group_pointer;
    gid_t groups[NGROUPS];
    u_int group_index;

    if (!suser())
        return;
    if (arguments->gidsetsize >
        sizeof(u.u_groups) / sizeof(u.u_groups[0])) {
        u.u_error = EINVAL;
        return;
    }
    u.u_error = copyin((caddr_t)arguments->gidset, (caddr_t)groups,
        arguments->gidsetsize * sizeof(u.u_groups[0]));
    if (u.u_error)
        return;
    for (group_index = 0; group_index < arguments->gidsetsize;
        group_index++) {
        if (groups[group_index] == NOGROUP) {
            u.u_error = EINVAL;
            return;
        }
    }
    bcopy(groups, u.u_groups,
        arguments->gidsetsize * sizeof(groups[0]));
    for (group_pointer = &u.u_groups[arguments->gidsetsize];
        group_pointer < &u.u_groups[NGROUPS]; group_pointer++)
        *group_pointer = NOGROUP;
}

/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid_t group)
{
    gid_t *group_pointer;

    for (group_pointer = u.u_groups;
        group_pointer < &u.u_groups[NGROUPS] &&
        *group_pointer != NOGROUP; group_pointer++)
        if (*group_pointer == group)
            return (1);
    return (0);
}
