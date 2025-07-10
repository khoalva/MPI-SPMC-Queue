#ifndef MPI_LIB_H
#define MPI_LIB_H

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mpi_lib.h
 * @brief MPI Wrapper Library - Making MPI calls easier and more robust
 * @version 1.0.0
 * @author MPI Library Team
 * 
 * This library provides:
 * - Simplified API with sensible defaults
 * - Automatic error handling and reporting
 * - Memory management helpers
 * - Window management utilities
 * - One-sided communication wrappers
 * - Collective communication wrappers
 * 
 * @example
 * ```c
 * #include "mpi_lib.h"
 * 
 * int main(int argc, char *argv[]) {
 *     mpi_context_t ctx;
 *     
 *     // Initialize MPI with automatic error handling
 *     if (mpi_init(argc, argv, &ctx) != MPI_SUCCESS) {
 *         return 1;
 *     }
 *     
 *     // Use simplified MPI calls
 *     if (mpi_is_root(&ctx)) {
 *         printf("Hello from root process!\n");
 *     }
 *     
 *     mpi_barrier(ctx.comm);
 *     mpi_finalize();
 *     return 0;
 * }
 * ```
 */

// Version information
#define MPI_LIB_VERSION_MAJOR 1
#define MPI_LIB_VERSION_MINOR 0
#define MPI_LIB_VERSION_PATCH 1

// ============================================================================
// ERROR HANDLING MACROS
// ============================================================================

/**
 * @brief Check MPI call and abort on error
 * @details This macro checks the return value of an MPI call and aborts
 *          the program if an error occurs. Use this for critical operations
 *          where failure should terminate the program.
 * @param call The MPI function call to check
 */
#define MPI_CHECK(call) do { \
    int err = (call); \
    if (err != MPI_SUCCESS) { \
        mpi_print_error(err, #call, __FILE__, __LINE__); \
        MPI_Abort(MPI_COMM_WORLD, err); \
    } \
} while(0)

/**
 * @brief Check MPI call and return error code
 * @details This macro checks the return value of an MPI call and returns
 *          the error code if an error occurs. Use this for operations
 *          where you want to handle errors gracefully.
 * @param call The MPI function call to check
 */
#define MPI_TRY(call) do { \
    int err = (call); \
    if (err != MPI_SUCCESS) { \
        mpi_print_error(err, #call, __FILE__, __LINE__); \
        return err; \
    } \
} while(0)

// ============================================================================
// CORE STRUCTURES
// ============================================================================

/**
 * @brief MPI context structure
 * @details Contains basic MPI environment information for a process
 */
typedef struct {
    int rank;           /**< Process rank in the communicator */
    int size;           /**< Total number of processes in the communicator */
    MPI_Comm comm;      /**< MPI communicator */
} mpi_context_t;

/**
 * @brief Window wrapper structure
 * @details Provides a convenient wrapper around MPI windows with
 *          additional metadata for easier management
 */
typedef struct {
    MPI_Win window;     /**< MPI window handle */
    void *base_ptr;     /**< Base pointer to window memory */
    size_t size;        /**< Size of window memory in bytes */
    int element_size;   /**< Size of each element in bytes */
    int is_valid;       /**< Flag indicating if window is valid (1) or not (0) */
} mpi_window_t;

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

/**
 * Initialize MPI and create context
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @param ctx Pointer to context structure to initialize
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_init(int argc, char *argv[], mpi_context_t *ctx);

/**
 * Initialize MPI with custom communicator
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @param comm MPI communicator to use
 * @param ctx Pointer to context structure to initialize
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_init_with_comm(int argc, char *argv[], MPI_Comm comm, mpi_context_t *ctx);

/**
 * Finalize MPI
 */
void mpi_finalize(void);

/**
 * Get library version string
 * @return Version string
 */
const char* mpi_get_version(void);

// ============================================================================
// WINDOW MANAGEMENT
// ============================================================================

/**
 * Create MPI window with automatic error handling
 * @param base Base pointer for window memory
 * @param size Size of memory in bytes
 * @param element_size Size of each element
 * @param comm MPI communicator
 * @param win Pointer to window structure to initialize
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_create(void *base, size_t size, int element_size, 
                   MPI_Comm comm, mpi_window_t *win);

/**
 * Create MPI window with MPI_Info
 * @param base Base pointer for window memory
 * @param size Size of memory in bytes
 * @param element_size Size of each element
 * @param info MPI_Info object
 * @param comm MPI communicator
 * @param win Pointer to window structure to initialize
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_create_with_info(void *base, size_t size, int element_size, 
                             MPI_Info info, MPI_Comm comm, mpi_window_t *win);

/**
 * Destroy MPI window
 * @param win Pointer to window structure
 */
void mpi_win_destroy(mpi_window_t *win);

/**
 * Lock window for passive target synchronization
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_lock_all(mpi_window_t *win);

/**
 * Unlock window
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_unlock_all(mpi_window_t *win);

/**
 * Lock multiple windows
 * @param windows Array of window structures
 * @param count Number of windows
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_lock_all_multiple(mpi_window_t *windows, int count);

/**
 * Unlock multiple windows
 * @param windows Array of window structures
 * @param count Number of windows
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_unlock_all_multiple(mpi_window_t *windows, int count);

/**
 * Wrapper for MPI_Win_fence with error handling
 * @param assert Fence assertion (usually 0)
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_fence(int assert, mpi_window_t *win);

/**
 * Lock window for a specific target (active/passive target synchronization)
 * @param lock_type Lock type (e.g., MPI_LOCK_SHARED or MPI_LOCK_EXCLUSIVE)
 * @param rank Target rank to lock (use MPI_PROC_NULL for all)
 * @param assert Assertion flags (usually 0)
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_lock(int lock_type, int rank, int assert, mpi_window_t *win);

/**
 * Unlock window for a specific target
 * @param rank Target rank to unlock (use MPI_PROC_NULL for all)
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_unlock(int rank, mpi_window_t *win);

/**
 * Wrapper for MPI_Win_flush with error handling
 * @param rank Target rank to flush
 * @param win Pointer to window structure
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_win_flush(int rank, mpi_window_t *win);

// ============================================================================
// ONE-SIDED COMMUNICATION
// ============================================================================

/**
 * Safe MPI_Put with automatic flushing and error handling
 * @param origin_addr Source data
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param target_rank Target process rank
 * @param target_offset Offset in target window (in bytes)
 * @param win Target window
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_put(const void *origin_addr, int count, MPI_Datatype datatype,
            int target_rank, size_t target_offset, mpi_window_t *win);

/**
 * Safe MPI_Get with automatic flushing and error handling
 * @param origin_addr Destination buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param target_rank Target process rank
 * @param target_offset Offset in target window (in bytes)
 * @param win Target window
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_get(void *origin_addr, int count, MPI_Datatype datatype,
            int target_rank, size_t target_offset, mpi_window_t *win);

/**
 * Safe MPI_Compare_and_swap with automatic flushing and error handling
 * @param origin_addr New value to store
 * @param compare_addr Value to compare against
 * @param result_addr Buffer to store old value
 * @param datatype MPI datatype
 * @param target_rank Target process rank
 * @param target_offset Offset in target window (in bytes)
 * @param win Target window
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_compare_and_swap(const void *origin_addr, const void *compare_addr,
                         void *result_addr, MPI_Datatype datatype,
                         int target_rank, size_t target_offset, mpi_window_t *win);

/**
 * Safe MPI_Fetch_and_op with automatic flushing and error handling
 * @param origin_addr Value to use in operation
 * @param result_addr Buffer to store old value
 * @param datatype MPI datatype
 * @param target_rank Target process rank
 * @param target_offset Offset in target window (in bytes)
 * @param op MPI operation
 * @param win Target window
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_fetch_and_op(const void *origin_addr, void *result_addr,
                     MPI_Datatype datatype, int target_rank,
                     size_t target_offset, MPI_Op op, mpi_window_t *win);

// ============================================================================
// POINT-TO-POINT COMMUNICATION
// ============================================================================

/**
 * Safe MPI_Send with error handling
 * @param buf Send buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param dest Destination rank
 * @param tag Message tag
 * @param comm MPI communicator
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_send(const void *buf, int count, MPI_Datatype datatype,
             int dest, int tag, MPI_Comm comm);

/**
 * Safe MPI_Recv with error handling
 * @param buf Receive buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param source Source rank (or MPI_ANY_SOURCE)
 * @param tag Message tag (or MPI_ANY_TAG)
 * @param comm MPI communicator
 * @param status Status object (can be MPI_STATUS_IGNORE)
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_recv(void *buf, int count, MPI_Datatype datatype,
             int source, int tag, MPI_Comm comm, MPI_Status *status);

/**
 * Safe MPI_Isend with error handling
 * @param buf Send buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param dest Destination rank
 * @param tag Message tag
 * @param comm MPI communicator
 * @param request Request handle
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_isend(const void *buf, int count, MPI_Datatype datatype,
              int dest, int tag, MPI_Comm comm, MPI_Request *request);

/**
 * Safe MPI_Irecv with error handling
 * @param buf Receive buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param source Source rank (or MPI_ANY_SOURCE)
 * @param tag Message tag (or MPI_ANY_TAG)
 * @param comm MPI communicator
 * @param request Request handle
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_irecv(void *buf, int count, MPI_Datatype datatype,
              int source, int tag, MPI_Comm comm, MPI_Request *request);

// ============================================================================
// COLLECTIVE COMMUNICATION
// ============================================================================

/**
 * Safe MPI_Barrier with error handling
 * @param comm MPI communicator
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_barrier(MPI_Comm comm);

/**
 * Safe MPI_Bcast with error handling
 * @param buffer Data buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param root Root process rank
 * @param comm MPI communicator
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_bcast(void *buffer, int count, MPI_Datatype datatype,
              int root, MPI_Comm comm);

/**
 * Safe MPI_Reduce with error handling
 * @param sendbuf Send buffer
 * @param recvbuf Receive buffer (only significant at root)
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param op MPI operation
 * @param root Root process rank
 * @param comm MPI communicator
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_reduce(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm);

/**
 * Safe MPI_Allreduce with error handling
 * @param sendbuf Send buffer
 * @param recvbuf Receive buffer
 * @param count Number of elements
 * @param datatype MPI datatype
 * @param op MPI operation
 * @param comm MPI communicator
 * @return MPI_SUCCESS on success, error code otherwise
 */
int mpi_allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

/**
 * Allocate memory on specified rank only
 * @param size Size in bytes
 * @param rank Rank to allocate on
 * @param current_rank Current process rank
 * @return Pointer to allocated memory, or NULL
 */
void* mpi_malloc(size_t size, int rank, int current_rank);

/**
 * Allocate and zero-initialize memory on specified rank only
 * @param size Size in bytes
 * @param rank Rank to allocate on
 * @param current_rank Current process rank
 * @return Pointer to allocated memory, or NULL
 */
void* mpi_calloc(size_t size, int rank, int current_rank);

/**
 * Free memory on specified rank only
 * @param ptr Pointer to free
 * @param rank Rank to free on
 * @param current_rank Current process rank
 */
void mpi_free(void *ptr, int rank, int current_rank);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Print MPI error with context information
 * @param error_code MPI error code
 * @param call_name Name of the MPI call that failed
 * @param file Source file name
 * @param line Line number
 */
void mpi_print_error(int error_code, const char *call_name, 
                     const char *file, int line);

/**
 * Check if current process is root (rank 0)
 * @param ctx MPI context
 * @return 1 if root, 0 otherwise
 */
int mpi_is_root(const mpi_context_t *ctx);

/**
 * Get current process rank
 * @param ctx MPI context
 * @return Process rank
 */
int mpi_get_rank(const mpi_context_t *ctx);

/**
 * Get total number of processes
 * @param ctx MPI context
 * @return Number of processes
 */
int mpi_get_size(const mpi_context_t *ctx);

/**
 * Print basic MPI information
 * @param ctx MPI context
 */
void mpi_print_info(const mpi_context_t *ctx);

// ============================================================================
// CONVENIENCE MACROS AND CONSTANTS
// ============================================================================

// Common data type shortcuts
#define MPI_LIB_INT        MPI_INT
#define MPI_LIB_DOUBLE     MPI_DOUBLE
#define MPI_LIB_FLOAT      MPI_FLOAT
#define MPI_LIB_CHAR       MPI_CHAR
#define MPI_LIB_BYTE       MPI_BYTE

// Common operations
#define MPI_LIB_SUM        MPI_SUM
#define MPI_LIB_MAX        MPI_MAX
#define MPI_LIB_MIN        MPI_MIN
#define MPI_LIB_PROD       MPI_PROD

// Message tags and sources
#define MPI_LIB_ANY_TAG    MPI_ANY_TAG
#define MPI_LIB_ANY_SOURCE MPI_ANY_SOURCE

// Status shortcuts
#define MPI_LIB_STATUS_IGNORE MPI_STATUS_IGNORE

// Common success/failure patterns
#define MPI_LIB_SUCCESS    MPI_SUCCESS
#define MPI_LIB_FAILURE    (-1)

/**
 * @brief Convenience macro for rank-specific code execution
 * @param ctx_ptr MPI context pointer
 * @param target_rank Target rank
 * @param code Code block to execute
 */
#define MPI_ON_RANK(ctx_ptr, target_rank, code) \
    do { \
        if ((ctx_ptr) && (ctx_ptr)->rank == (target_rank)) { \
            code \
        } \
    } while(0)

/**
 * @brief Convenience macro for root-only code execution
 * @param ctx_ptr MPI context pointer
 * @param code Code block to execute
 */
#define MPI_ON_ROOT(ctx_ptr, code) MPI_ON_RANK(ctx_ptr, 0, code)

#ifdef __cplusplus
}
#endif

#endif // MPI_LIB_H
