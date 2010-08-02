#ifndef __PAMAURY_CONFIG_HPP__
#define __PAMAURY_CONFIG_HPP__

#define PAMAURY_SCHEDULER_NS    pasched

#cmakedefine HAS_SYMHPONY
#cmakedefine ENABLE_DAG_AUTO_CHECK_CONSISTENCY
#cmakedefine ENABLE_SCHED_AUTO_CHECK_RP
#cmakedefine ENABLE_EXPENSIVE_SCHED_AUTO_CHECK_RP
#cmakedefine ENABLE_XFORM_AUTO_CHECK_RP
#cmakedefine ENABLE_XFORM_TIME_STAT
#cmakedefine ENABLE_SCHED_TIME_STAT
#cmakedefine ENABLE_MISC_TIME_STAT

#ifdef ENABLE_XFORM_TIME_STAT
#define XTM_STAT(a) a
#else
#define XTM_STAT(a)
#endif

#ifdef ENABLE_SCHED_TIME_STAT
#define STM_STAT(a) a
#else
#define STM_STAT(a)
#endif

#ifdef ENABLE_MISC_TIME_STAT
#define MTM_STAT(a) a
#else
#define MTM_STAT(a)
#endif

#endif // __PAMAURY_CONFIG_HPP__
