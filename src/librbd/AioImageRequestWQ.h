// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_AIO_IMAGE_REQUEST_WQ_H
#define CEPH_LIBRBD_AIO_IMAGE_REQUEST_WQ_H

#include "include/Context.h"
#include "include/atomic.h"
#include "common/RWLock.h"
#include "common/WorkQueue.h"
#include <list>

namespace librbd {

class AioCompletion;
template <typename> class AioImageRequest;
class ImageCtx;

enum AioDirection {
  AIO_DIRECTION_READ,
  AIO_DIRECTION_WRITE,
  AIO_DIRECTION_BOTH
};

template <typename ImageCtxT = librbd::ImageCtx>
class AioImageRequestWQ
  : protected ThreadPool::PointerWQ<AioImageRequest<ImageCtxT> > {
public:
  AioImageRequestWQ(ImageCtxT *image_ctx, const string &name, time_t ti,
                    ThreadPool *tp);

  ssize_t read(uint64_t off, uint64_t len, char *buf, int op_flags);
  ssize_t write(uint64_t off, uint64_t len, const char *buf, int op_flags);
  int discard(uint64_t off, uint64_t len);

  void aio_read(AioCompletion *c, uint64_t off, uint64_t len, char *buf,
                bufferlist *pbl, int op_flags, bool native_async=true);
  void aio_write(AioCompletion *c, uint64_t off, uint64_t len, const char *buf,
                 int op_flags, bool native_async=true);
  void aio_discard(AioCompletion *c, uint64_t off, uint64_t len,
                   bool native_async=true);
  void aio_flush(AioCompletion *c, bool native_async=true);

  using typename ThreadPool::PointerWQ<AioImageRequest<ImageCtxT> >::drain;
  using typename ThreadPool::PointerWQ<AioImageRequest<ImageCtxT> >::empty;

  void shut_down(Context *on_shutdown);

  bool is_lock_required() const;
  bool is_lock_request_needed() const;

  inline bool writes_blocked() const {
    RWLock::RLocker locker(m_lock);
    return (m_write_blockers > 0);
  }

  int block_writes();
  void block_writes(Context *on_blocked);
  void unblock_writes();

  void set_require_lock(AioDirection aio_direction, bool enabled);

protected:
  virtual void *_void_dequeue();
  virtual void process(AioImageRequest<ImageCtxT> *req);

private:
  typedef std::list<Context *> Contexts;

  struct C_RefreshFinish : public Context {
    AioImageRequestWQ *aio_work_queue;
    AioImageRequest<ImageCtxT> *aio_image_request;

    C_RefreshFinish(AioImageRequestWQ *aio_work_queue,
                    AioImageRequest<ImageCtxT> *aio_image_request)
      : aio_work_queue(aio_work_queue), aio_image_request(aio_image_request) {
    }
    virtual void finish(int r) override {
      aio_work_queue->handle_refreshed(r, aio_image_request);
    }
  };

  struct C_BlockedWrites : public Context {
    AioImageRequestWQ *aio_work_queue;
    C_BlockedWrites(AioImageRequestWQ *_aio_work_queue)
      : aio_work_queue(_aio_work_queue) {
    }

    virtual void finish(int r) {
      aio_work_queue->handle_blocked_writes(r);
    }
  };

  ImageCtxT &m_image_ctx;
  mutable RWLock m_lock;
  Contexts m_write_blocker_contexts;
  uint32_t m_write_blockers = 0;
  bool m_require_lock_on_read = false;
  bool m_require_lock_on_write = false;
  atomic_t m_in_flight_writes {0};
  atomic_t m_queued_reads {0};
  atomic_t m_queued_writes {0};
  atomic_t m_in_flight_ios {0};

  bool m_refresh_in_progress = false;

  bool m_shutdown = false;
  Context *m_on_shutdown = nullptr;

  inline bool writes_empty() const {
    RWLock::RLocker locker(m_lock);
    return (m_queued_writes.read() == 0);
  }

  void finish_queued_io(AioImageRequest<ImageCtxT> *req);
  void finish_in_flight_write();

  int start_in_flight_io(AioCompletion *c);
  void finish_in_flight_io();

  void queue(AioImageRequest<ImageCtxT> *req);

  void handle_refreshed(int r, AioImageRequest<ImageCtxT> *req);
  void handle_blocked_writes(int r);
};

} // namespace librbd

extern template class librbd::AioImageRequestWQ<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_AIO_IMAGE_REQUEST_WQ_H
