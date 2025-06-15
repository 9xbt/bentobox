const char *__kernel_name = "bentobox";

int __kernel_version_major = 0;
int __kernel_version_minor = 2;

const char *__kernel_build_date = __DATE__;
const char *__kernel_build_time = __TIME__;

#ifdef __x86_64__
const char *__kernel_arch = "x86_64";
#elif __riscv
const char *__kernel_arch = "riscv";
#else
const char *__kernel_arch = "unknown";
#endif

#ifdef GIT_COMMIT_HASH
const char *__kernel_commit_hash = "dirty " GIT_COMMIT_HASH;
#else
const char *__kernel_commit_hash = "dirty unknown";
#endif