/**
 * @file mpi_lib.c
 * @brief Implementation of MPI wrapper library
 * @version 1.0.0
 * 
 * This file implements the MPI wrapper functions defined in mpi_lib.h,
 * providing simplified interfaces with robust error handling.
 */

#include "mpi_lib.h"
#include <string.h>

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

int mpi_init(int argc, char *argv[], mpi_context_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL context pointer in mpi_init\n");
        return MPI_ERR_ARG;
    }
    
    int err = MPI_Init(&argc, &argv);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Init", __FILE__, __LINE__);
        return err;
    }
    
    return mpi_init_with_comm(argc, argv, MPI_COMM_WORLD, ctx);
}

int mpi_init_with_comm(int argc, char *argv[], MPI_Comm comm, mpi_context_t *ctx) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;
    
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL context pointer in mpi_init_with_comm\n");
        return MPI_ERR_ARG;
    }
    
    // Initialize context structure
    ctx->comm = comm;
    ctx->rank = -1;
    ctx->size = -1;
    
    int err = MPI_Comm_rank(ctx->comm, &ctx->rank);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Comm_rank", __FILE__, __LINE__);
        return err;
    }
    
    err = MPI_Comm_size(ctx->comm, &ctx->size);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Comm_size", __FILE__, __LINE__);
        return err;
    }
    
    return MPI_SUCCESS;
}

void mpi_finalize(void) {
    MPI_Finalize();
}

const char* mpi_get_version(void) {
    static char version[64];
    snprintf(version, sizeof(version), "%d.%d.%d", 
             MPI_LIB_VERSION_MAJOR, MPI_LIB_VERSION_MINOR, MPI_LIB_VERSION_PATCH);
    return version;
}

// ============================================================================
// WINDOW MANAGEMENT
// ============================================================================

int mpi_win_create(void *base, size_t size, int element_size, 
                   MPI_Comm comm, mpi_window_t *win) {
    return mpi_win_create_with_info(base, size, element_size, MPI_INFO_NULL, comm, win);
}

int mpi_win_create_with_info(void *base, size_t size, int element_size, 
                             MPI_Info info, MPI_Comm comm, mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_create_with_info\n");
        return MPI_ERR_ARG;
    }
    
    if (element_size <= 0) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid element_size (%d) in mpi_win_create_with_info\n", element_size);
        return MPI_ERR_ARG;
    }
    
    // Initialize window structure
    win->base_ptr = base;
    win->size = size;
    win->element_size = element_size;
    win->is_valid = 0;
    win->window = MPI_WIN_NULL;
    
    int err = MPI_Win_create(base, size, element_size, info, comm, &win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_create", __FILE__, __LINE__);
        return err;
    }
    
    win->is_valid = 1;
    return MPI_SUCCESS;
}

void mpi_win_destroy(mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB WARNING] NULL window pointer in mpi_win_destroy\n");
        return;
    }
    
    if (!win->is_valid || win->window == MPI_WIN_NULL) {
        return;
    }
    
    int err = MPI_Win_free(&win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_free", __FILE__, __LINE__);
    }
    
    // Reset window structure
    win->base_ptr = NULL;
    win->size = 0;
    win->element_size = 0;
    win->is_valid = 0;
    win->window = MPI_WIN_NULL;
}

int mpi_win_lock_all(mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_lock_all\n");
        return MPI_ERR_ARG;
    }
    
    if (!win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_win_lock_all\n");
        return MPI_ERR_ARG;
    }
    
    int err = MPI_Win_lock_all(0, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_lock_all", __FILE__, __LINE__);
    }
    return err;
}

int mpi_win_unlock_all(mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_unlock_all\n");
        return MPI_ERR_ARG;
    }
    
    if (!win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_win_unlock_all\n");
        return MPI_ERR_ARG;
    }
    
    int err = MPI_Win_unlock_all(win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_unlock_all", __FILE__, __LINE__);
    }
    return err;
}

int mpi_win_lock_all_multiple(mpi_window_t *windows, int count) {
    if (!windows) return MPI_ERR_ARG;
    
    for (int i = 0; i < count; i++) {
        int err = mpi_win_lock_all(&windows[i]);
        if (err != MPI_SUCCESS) {
            // Try to unlock already locked windows
            for (int j = 0; j < i; j++) {
                mpi_win_unlock_all(&windows[j]);
            }
            return err;
        }
    }
    return MPI_SUCCESS;
}

int mpi_win_unlock_all_multiple(mpi_window_t *windows, int count) {
    if (!windows) return MPI_ERR_ARG;
    
    int last_error = MPI_SUCCESS;
    for (int i = 0; i < count; i++) {
        int err = mpi_win_unlock_all(&windows[i]);
        if (err != MPI_SUCCESS) {
            last_error = err;
        }
    }
    return last_error;
}

int mpi_win_fence(int assert, mpi_window_t *win) {
    if (!win || !win->is_valid) return MPI_ERR_ARG;
    int err = MPI_Win_fence(assert, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_fence", __FILE__, __LINE__);
    }
    return err;
}

// ============================================================================
// ONE-SIDED COMMUNICATION
// ============================================================================

int mpi_put(const void *origin_addr, int count, MPI_Datatype datatype,
            int target_rank, size_t target_offset, mpi_window_t *win) {
    if (!origin_addr) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL origin_addr in mpi_put\n");
        return MPI_ERR_ARG;
    }
    
    if (!win || !win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_put\n");
        return MPI_ERR_ARG;
    }
    
    if (count <= 0) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid count (%d) in mpi_put\n", count);
        return MPI_ERR_ARG;
    }
    
    if (win->element_size <= 0) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid element_size in window\n");
        return MPI_ERR_ARG;
    }
    
    MPI_Aint displacement = target_offset / win->element_size;
    
    int err = MPI_Put(origin_addr, count, datatype, target_rank, displacement, 
                      count, datatype, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Put", __FILE__, __LINE__);
        return err;
    }
    
    err = MPI_Win_flush(target_rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_flush", __FILE__, __LINE__);
    }
    return err;
}

int mpi_get(void *origin_addr, int count, MPI_Datatype datatype,
            int target_rank, size_t target_offset, mpi_window_t *win) {
    if (!origin_addr || !win || !win->is_valid) return MPI_ERR_ARG;
    
    MPI_Aint displacement = target_offset / win->element_size;
    
    int err = MPI_Get(origin_addr, count, datatype, target_rank, displacement, 
                      count, datatype, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Get", __FILE__, __LINE__);
        return err;
    }
    
    err = MPI_Win_flush(target_rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_flush", __FILE__, __LINE__);
    }
    return err;
}

int mpi_compare_and_swap(const void *origin_addr, const void *compare_addr,
                         void *result_addr, MPI_Datatype datatype,
                         int target_rank, size_t target_offset, mpi_window_t *win) {
    if (!origin_addr || !compare_addr || !result_addr || !win || !win->is_valid) {
        return MPI_ERR_ARG;
    }
    
    MPI_Aint displacement = target_offset / win->element_size;
    
    int err = MPI_Compare_and_swap(origin_addr, compare_addr, result_addr, datatype,
                                   target_rank, displacement, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Compare_and_swap", __FILE__, __LINE__);
        return err;
    }
    
    err = MPI_Win_flush(target_rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_flush", __FILE__, __LINE__);
    }
    return err;
}

int mpi_fetch_and_op(const void *origin_addr, void *result_addr,
                     MPI_Datatype datatype, int target_rank,
                     size_t target_offset, MPI_Op op, mpi_window_t *win) {
    if (!origin_addr || !result_addr || !win || !win->is_valid) return MPI_ERR_ARG;
    
    MPI_Aint displacement = target_offset / win->element_size;
    
    int err = MPI_Fetch_and_op(origin_addr, result_addr, datatype, target_rank,
                               displacement, op, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Fetch_and_op", __FILE__, __LINE__);
        return err;
    }
    
    err = MPI_Win_flush(target_rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_flush", __FILE__, __LINE__);
    }
    return err;
}

// ============================================================================
// POINT-TO-POINT COMMUNICATION
// ============================================================================

int mpi_send(const void *buf, int count, MPI_Datatype datatype,
             int dest, int tag, MPI_Comm comm) {
    if (!buf && count > 0) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL buffer with positive count in mpi_send\n");
        return MPI_ERR_ARG;
    }
    
    if (count < 0) {
        fprintf(stderr, "[MPI_LIB ERROR] Negative count (%d) in mpi_send\n", count);
        return MPI_ERR_ARG;
    }
    
    int err = MPI_Send(buf, count, datatype, dest, tag, comm);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Send", __FILE__, __LINE__);
    }
    return err;
}

int mpi_recv(void *buf, int count, MPI_Datatype datatype,
             int source, int tag, MPI_Comm comm, MPI_Status *status) {
    if (!buf && count > 0) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL buffer with positive count in mpi_recv\n");
        return MPI_ERR_ARG;
    }
    
    if (count < 0) {
        fprintf(stderr, "[MPI_LIB ERROR] Negative count (%d) in mpi_recv\n", count);
        return MPI_ERR_ARG;
    }
    
    int err = MPI_Recv(buf, count, datatype, source, tag, comm, status);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Recv", __FILE__, __LINE__);
    }
    return err;
}

int mpi_isend(const void *buf, int count, MPI_Datatype datatype,
              int dest, int tag, MPI_Comm comm, MPI_Request *request) {
    int err = MPI_Isend(buf, count, datatype, dest, tag, comm, request);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Isend", __FILE__, __LINE__);
    }
    return err;
}

int mpi_irecv(void *buf, int count, MPI_Datatype datatype,
              int source, int tag, MPI_Comm comm, MPI_Request *request) {
    int err = MPI_Irecv(buf, count, datatype, source, tag, comm, request);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Irecv", __FILE__, __LINE__);
    }
    return err;
}

// ============================================================================
// COLLECTIVE COMMUNICATION
// ============================================================================

int mpi_barrier(MPI_Comm comm) {
    int err = MPI_Barrier(comm);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Barrier", __FILE__, __LINE__);
    }
    return err;
}

int mpi_bcast(void *buffer, int count, MPI_Datatype datatype,
              int root, MPI_Comm comm) {
    int err = MPI_Bcast(buffer, count, datatype, root, comm);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Bcast", __FILE__, __LINE__);
    }
    return err;
}

int mpi_reduce(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    int err = MPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Reduce", __FILE__, __LINE__);
    }
    return err;
}

int mpi_allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    int err = MPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Allreduce", __FILE__, __LINE__);
    }
    return err;
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void* mpi_malloc(size_t size, int rank, int current_rank) {
    if (size == 0) {
        fprintf(stderr, "[MPI_LIB WARNING] Zero size allocation requested on rank %d\n", current_rank);
        return NULL;
    }
    
    if (rank == current_rank) {
        void *ptr = malloc(size);
        if (!ptr) {
            fprintf(stderr, "[MPI_LIB ERROR] Memory allocation failed for size %zu on rank %d\n", 
                    size, current_rank);
        }
        return ptr;
    }
    return NULL;
}

void* mpi_calloc(size_t size, int rank, int current_rank) {
    if (size == 0) {
        fprintf(stderr, "[MPI_LIB WARNING] Zero size allocation requested on rank %d\n", current_rank);
        return NULL;
    }
    
    if (rank == current_rank) {
        void *ptr = calloc(1, size);
        if (!ptr) {
            fprintf(stderr, "[MPI_LIB ERROR] Memory allocation failed for size %zu on rank %d\n", 
                    size, current_rank);
        }
        return ptr;
    }
    return NULL;
}

void mpi_free(void *ptr, int rank, int current_rank) {
    if (rank == current_rank) {
        if (ptr) {
            free(ptr);
        } else {
            fprintf(stderr, "[MPI_LIB WARNING] Attempting to free NULL pointer on rank %d\n", current_rank);
        }
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void mpi_print_error(int error_code, const char *call_name, 
                     const char *file, int line) {
    char error_string[MPI_MAX_ERROR_STRING];
    int error_string_length;
    
    if (!call_name) call_name = "unknown";
    if (!file) file = "unknown";
    
    int err = MPI_Error_string(error_code, error_string, &error_string_length);
    if (err != MPI_SUCCESS) {
        snprintf(error_string, sizeof(error_string), "Error code %d (failed to get error string)", error_code);
    }
    
    fprintf(stderr, "[MPI_LIB ERROR] %s failed at %s:%d\n", call_name, file, line);
    fprintf(stderr, "  Error: %s (code: %d)\n", error_string, error_code);
    fflush(stderr);
}

int mpi_is_root(const mpi_context_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB WARNING] NULL context in mpi_is_root\n");
        return 0;
    }
    return ctx->rank == 0;
}

int mpi_get_rank(const mpi_context_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB WARNING] NULL context in mpi_get_rank\n");
        return -1;
    }
    return ctx->rank;
}

int mpi_get_size(const mpi_context_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB WARNING] NULL context in mpi_get_size\n");
        return -1;
    }
    return ctx->size;
}

void mpi_print_info(const mpi_context_t *ctx) {
    if (!ctx) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid MPI context\n");
        return;
    }
    
    printf("MPI Library v%s\n", mpi_get_version());
    printf("Process %d of %d processes\n", ctx->rank, ctx->size);
    
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    if (MPI_Get_processor_name(processor_name, &name_len) == MPI_SUCCESS) {
        printf("Running on processor: %s\n", processor_name);
    } else {
        printf("Unable to get processor name\n");
    }
    fflush(stdout);
}

int mpi_win_lock(int lock_type, int rank, int assert, mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_lock\n");
        return MPI_ERR_ARG;
    }
    if (!win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_win_lock\n");
        return MPI_ERR_ARG;
    }
    int err = MPI_Win_lock(lock_type, rank, assert, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_lock", __FILE__, __LINE__);
    }
    return err;
}

int mpi_win_unlock(int rank, mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_unlock\n");
        return MPI_ERR_ARG;
    }
    if (!win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_win_unlock\n");
        return MPI_ERR_ARG;
    }
    int err = MPI_Win_unlock(rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_unlock", __FILE__, __LINE__);
    }
    return err;
}

int mpi_win_flush(int rank, mpi_window_t *win) {
    if (!win) {
        fprintf(stderr, "[MPI_LIB ERROR] NULL window pointer in mpi_win_flush\n");
        return MPI_ERR_ARG;
    }
    if (!win->is_valid) {
        fprintf(stderr, "[MPI_LIB ERROR] Invalid window in mpi_win_flush\n");
        return MPI_ERR_ARG;
    }
    int err = MPI_Win_flush(rank, win->window);
    if (err != MPI_SUCCESS) {
        mpi_print_error(err, "MPI_Win_flush", __FILE__, __LINE__);
    }
    return err;
}
