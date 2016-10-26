// Utility routines for the main bess daemon.

#ifndef BESS_BESS_CORE_BESSD_H_
#define BESS_BESS_CORE_BESSD_H_

#include <tuple>

namespace bess {
namespace bessd {

// Process command line arguments from gflags.
void ProcessCommandLineArgs();

// Checks that we are running as superuser.
void CheckRunningAsRoot();

// Write the pid value to the given file fd.  Overwrites anything present at that fd.
// Dies if unable to overwrite the file.
void WritePidfile(int fd, pid_t pid);

// Read the pid value from the given file fd.  Returns true and the read pid value upon success.  Returns false upon failure.
std::tuple<bool, pid_t> ReadPidfile(int fd);

// Tries to acquire the daemon pidfile lock for the file open at the given fd.  Dies if an
// error occurs when trying to acquire the lock.  Returns a pair <lockheld, pid> where
// lockheld is a bool indicating if the lock is held and pid is a pid_t that is non-zero
// if lockheld is true indicating the process holding the lock.
std::tuple<bool, pid_t> TryAcquirePidfileLock(int fd);

// Ensures that we are a unique instance.
void CheckUniqueInstance(const std::string &pidfile_path);

// Starts BESS as a daemon running in the background.
int StartDaemon();

// Sets BESS's resource limit.
void SetResourceLimit();

// Initializes all drivers.
void InitDrivers();

}  // namespace bessd
}  // namespace bess

#endif  // BESS_CORE_BESSD_H_
