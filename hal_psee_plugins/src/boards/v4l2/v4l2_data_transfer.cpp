/**********************************************************************************************************************
 * Copyright (c) Prophesee S.A.                                                                                       *
 *                                                                                                                    *
 * Licensed under the Apache License, Version 2.0 (the "License");                                                    *
 * you may not use this file except in compliance with the License.                                                   *
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0                                 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed   *
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.                      *
 * See the License for the specific language governing permissions and limitations under the License.                 *
 **********************************************************************************************************************/

#include <memory>
#include <system_error>
#include <unistd.h>

#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>

#include "boards/v4l2/v4l2_data_transfer.h"
#include "boards/v4l2/dma_buf_heap.h"

#include "metavision/hal/utils/data_transfer.h"
#include "metavision/hal/utils/hal_log.h"

namespace Metavision {

static constexpr bool allow_buffer_drop              = true;
static constexpr size_t device_buffer_preload_number = 4;

V4l2DataTransfer::V4l2DataTransfer(int fd, const StreamFormat& format, uint32_t raw_event_size_bytes) :
    memtype_(V4L2_MEMORY_MMAP),
    fd_(dup(fd)),
    v4l2_allocator_(std::make_unique<V4l2MmapAllocator>(fd)),
    pool_(ObjectPool::make_bounded(device_buffer_number, Allocator{v4l2_allocator_.get()})) {
    auto res = request_buffers(device_buffer_number);
    if (res.count != device_buffer_number)
        throw std::system_error(ENOMEM, std::generic_category(), "Unexpected amount of V4L2 buffers allocated");
    if(auto env = std::getenv("PSEE_VAR_V4L2_BSIZE"))
    {
        if(format.name() == "EVT3")
            infer_manual_buf_size = 3;
        else
            infer_manual_buf_size = 2; // Default to 2 for EVT2 and EVT21
    }
}

V4l2DataTransfer::V4l2DataTransfer(int fd, const StreamFormat& format, uint32_t raw_event_size_bytes, const std::string &heap_path,
                                   const std::string &heap_name) :
    memtype_(V4L2_MEMORY_DMABUF),
    fd_(dup(fd)),
    v4l2_allocator_(std::make_unique<DmabufAllocator>(fd, std::make_unique<DmaBufHeap>(heap_path, heap_name))),
    pool_(ObjectPool::make_bounded(device_buffer_number, Allocator{v4l2_allocator_.get()})) {
    auto res = request_buffers(device_buffer_number);
    if (res.count != device_buffer_number)
        throw std::system_error(ENOMEM, std::generic_category(), "Unexpected amount of V4L2 buffers allocated");
    if(auto env = std::getenv("PSEE_VAR_V4L2_BSIZE"))
    {
        if(format.name() == "EVT3")
            infer_manual_buf_size = 3;
        else
            infer_manual_buf_size = 2; // Default to 2 for EVT2 and EVT21
    }
}

V4l2DataTransfer::~V4l2DataTransfer() {
    // Release the previously acquired buffers
    try {
        request_buffers(0);
    } catch (const std::exception &e) { MV_HAL_LOG_TRACE() << "~V4l2DataTransfer:" << e.what(); }
    // and release this file handler
    close(fd_);
}

V4l2RequestBuffers V4l2DataTransfer::request_buffers(uint32_t nb_buffers) {
    struct v4l2_capability cap = {0};
    V4l2RequestBuffers req{.count = nb_buffers};

    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap)) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_QUERYCAP failed");
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        throw HalConnectionException(ENOTSUP, std::generic_category());
    }
    req.memory = memtype_;

    if (-1 == ioctl(fd_, VIDIOC_REQBUFS, &req)) {
        throw std::system_error(errno, std::generic_category(), "VIDIOC_REQBUFS failed");
    }

    buf_type_ = (enum v4l2_buf_type)req.type;
    return req;
}

void V4l2DataTransfer::start_impl() {
    MV_HAL_LOG_TRACE() << "V4l2DataTransfer - start_impl() ";

    // DMA usually need 2 buffers to run, to be able to quickly switch from one transfer to the next
    // The way datatransfer is built, run_impl sequentially dequeue a buffer, pass it to EventStream, query another
    // from the buffer pool, queue it, then dequeue the next buffer.
    // If all buffer are queued it becomes impossible to dequeue 2 of them to process them in parallel.
    // Since 2 queued buffers are usually enough, and we have 32 of them, queuing 4 should avoid issues with hardware
    // expecting more, while allowing 28 buffers in parallel (or in a 28-stage pipeline) in the app.
    for (unsigned int i = 0; i < device_buffer_preload_number; ++i) {
        struct v4l2_plane plane = {0};
        auto input_buff         = pool_.acquire();
        // Using DMABUF, the allocator handles the pool of buffers through file descriptors, we need to choose a free
        // index to queue a buffer.
        // On the other hand, with MMAP, the pool is handled through indices, and fill_v4l2_buffer will fix the index
        // in the V4l2Buffer descriptor.
        V4l2Buffer buffer = {.index = i, .type = buf_type_, .memory = memtype_};
        if (buffer.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            buffer.length   = 1;
            buffer.m.planes = &plane;
        }
        fill_v4l2_buffer(input_buff, buffer);

        // Update buffer size to its capacity so that it may only be downsized after transfer
        // upsizing buffer size may default-initialize content, erasing the actual data
        begin_cpu_access(input_buff);
        input_buff->resize(input_buff->capacity());
        end_cpu_access(input_buff);

        if (ioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            throw std::system_error(errno, std::generic_category(), "VIDIOC_QBUF failed");
        }
        queued_buffers_[buffer.index] = std::move(input_buff);
    }
}

void V4l2DataTransfer::run_impl(const DataTransfer &data_transfer) {
    MV_HAL_LOG_TRACE() << "V4l2DataTransfer - run_impl() ";
    struct pollfd fds[1];

    fds[0].fd      = fd_;
    fds[0].events  = POLLIN;
    fds[0].revents = 0;

    while (!data_transfer.should_stop()) {
        V4l2Buffer buf{0};
        struct v4l2_plane plane;
        uint32_t bytesused;

        if (poll(fds, 1, -1) < 0) {
            MV_HAL_LOG_ERROR() << "V4l2DataTransfer: poll failed" << strerror(errno);
            break;
        }

        if (fds[0].revents & POLLERR) {
            using namespace std::chrono_literals;
            // When stopping, STREAMOFF ioctl will return all buffers and epoll will signal an error, since there is no
            // queued buffer anymore. This will usually trig faster than calling DataTransfer::stop, and should_stop()
            // will still return false, even though I_EventStream is stopping.
            // But this may also happen at start. Wait a bit and retry until should_stop()
            MV_HAL_LOG_TRACE() << "V4l2DataTransfer: poll returned" << std::hex << fds[0].revents << std::dec;
            std::this_thread::sleep_for(1ms);
            continue;
        }

        buf.type   = buf_type_;
        buf.memory = memtype_;
        if (buf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            buf.length   = 1;
            buf.m.planes = &plane;
        }
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            MV_HAL_LOG_ERROR() << "V4l2DataTransfer: DQBUF failed" << strerror(errno);
            break;
        }

        if (buf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            bytesused = buf.m.planes[0].bytesused;
        } else {
            bytesused = buf.bytesused;
        }
        MV_HAL_LOG_DEBUG() << "Grabbed buffer" << buf.index << "of:" << bytesused << "Bytes.";

        // Advertise CPU operations to allow cache maintenance
        begin_cpu_access(queued_buffers_[buf.index]);

        if(infer_manual_buf_size > 0)
        {
            size_t new_size = buf.bytesused; // default: keep full size
            if(infer_manual_buf_size == 3) {
                // 16 bit (for evt3)
                uint16_t* buffer = reinterpret_cast<uint16_t*>(queued_buffers_[buf.index]->data());
                size_t new_size_16 = new_size / sizeof(uint16_t);
                for (size_t i = 0; i < new_size_16; ++i) {
                    if (buffer[i] == 0xE019) {
                        //MV_HAL_LOG_TRACE() << "Found eof marker at index" << i*sizeof(uint16_t);
                        new_size = i*sizeof(uint16_t); // keep up to this pattern
                        break;
                    }
                }
            }
            else //if(infer_manual_buf_size == 2 || infer_manual_buf_size == 21) 
            {
                // 64 bit for evt2 and evt21
                uint64_t* buffer = reinterpret_cast<uint64_t*>(queued_buffers_[buf.index]->data());
                size_t new_size_64 = new_size / sizeof(uint64_t);
                for (size_t i = 0; i < new_size_64; ++i) {
                    if ((buffer[i] & 0xE00000FF00000000) == 0xE000001900000000) {
                        //MV_HAL_LOG_TRACE() << "Found eof marker at index" << i*sizeof(uint64_t);
                        new_size = i*sizeof(uint64_t); // keep up to this pattern
                        break;
                    }
                }
            } 
            queued_buffers_[buf.index]->resize(new_size);
        }
        else {
            // Get the vector corresponding to this buffer and transfer the data
            queued_buffers_[buf.index]->resize(bytesused);
        }

        // Transfer the data for processing
        // if there is no more available buffer and buffer drop is allowed,
        // skip this operation to be able to re-queue the buffer immediatly
        if (!(allow_buffer_drop && pool_.empty())) {
            data_transfer.transfer_data(queued_buffers_[buf.index]);
        }

        // Release the buffer to DataTransfer BufferPool
        queued_buffers_[buf.index].reset();

        // Get the next buffer, possibly the one we just released (if not hold by data_transfer)
        auto next = pool_.acquire();

        // buf is filled with the info of the dequeued buffer
        // update it with next information
        fill_v4l2_buffer(next, buf);

        // Update buffer size to its capacity so that it may only be downsized after transfer
        // upsizing buffer size may default-initialize content, erasing the actual data
        next->resize(next->capacity());

        // Advertise end of CPU operations to allow cache maintenance
        end_cpu_access(next);

        // Queue the next buffer to keep the device running
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            throw std::system_error(errno, std::generic_category(), "VIDIOC_QBUF failed");
        }
        queued_buffers_[buf.index] = std::move(next);
    }
}

void V4l2DataTransfer::stop_impl() {
    MV_HAL_LOG_TRACE() << "V4l2DataTransfer - stop_impl() ";
    // Here we trust that I_EventStream has also stopped V4L2 Device Control, which does STREAMOFF
    // and return every buffer, allowing to release buffers here
    for (size_t i = 0; i < device_buffer_number; i++)
        queued_buffers_[i].reset();
}

V4l2DataTransfer::V4l2Allocator &V4l2DataTransfer::v4l2_alloc(BufferPtr &buf) const {
    // The std::vectors in BufferPool are built with a V4l2Allocator, which can do this work
    V4l2Allocator *alloc = dynamic_cast<V4l2Allocator *>(buf->get_allocator().resource());
    if (!alloc)
        throw std::system_error(EPERM, std::generic_category(), "Memory resource is expected to be V4l2Allocator");
    return *alloc;
}

// Fills the V4L2 descriptor with the buffer info. This information depends on the Allocator implementation
// For instance in MMAP the buffer is mapped to an index of the device
// But on DMABUF, the buffer is mapped to a standalone file descriptor
// fill_v4l2_buffer requires v4l2_buf to be filled with a non-queued index of the V4L2 device, but may
// rewrite it, in case there is a fixed mapping between buffers and indices, as in MMAP use case
// The caller shall not assume that v4l2_buf.index is unchanged after a call to fill_v4l2_buffer
void V4l2DataTransfer::fill_v4l2_buffer(BufferPtr &buf, V4l2Buffer &v4l2_buf) const {
    // Since we inherit from DataTransfer, which requires a BufferPool, we can't query the actual buffer
    // size before building the BufferPool, and it is built with empty buffers. This ensure the vectors
    // are allocated before trying to map their allocations with the V4L2 buffers
    if (!buf->data()) {
        if (auto *v4l2_alloc = dynamic_cast<V4l2Allocator *>(buf->get_allocator().resource())) {
            buf->reserve(v4l2_alloc->max_byte_size());
        } else {
            MV_HAL_LOG_ERROR() << "V4l2DataTransfer - Resource allocator should implement 'V4l2Allocator'";
        }
    }

    v4l2_alloc(buf).fill_v4l2_buffer(buf->data(), v4l2_buf);
}

// Call the cache maintenance that fits the memory type
void V4l2DataTransfer::begin_cpu_access(BufferPtr &buf) const {
    v4l2_alloc(buf).begin_cpu_access(buf->data());
}

void V4l2DataTransfer::end_cpu_access(BufferPtr &buf) const {
    v4l2_alloc(buf).end_cpu_access(buf->data());
}

V4l2DataTransfer::V4l2Allocator::V4l2Allocator(int videodev_fd) {
    struct v4l2_capability cap = {0};

    if (ioctl(videodev_fd, VIDIOC_QUERYCAP, &cap)) {
        throw HalConnectionException(errno, std::generic_category(), "VIDIOC_QUERYCAP failed");
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        throw HalConnectionException(ENOTSUP, std::generic_category());
    }

    struct v4l2_format format = {.type = buf_type_};
    // Technically, the format is not locked yet, it will be locked when V4l2DataTransfer constructor does
    // request_buffers, but we need to build the BufferPool with an Allocator first
    if (ioctl(videodev_fd, VIDIOC_G_FMT, &format))
        throw std::system_error(errno, std::generic_category(), "VIDIOC_G_FMT failed");

    if (buf_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        buffer_byte_size_ = format.fmt.pix_mp.plane_fmt[0].sizeimage;
    } else {
        buffer_byte_size_ = format.fmt.pix.sizeimage;
    }
}

V4l2BufType V4l2DataTransfer::V4l2Allocator::get_buf_type() const {
    return buf_type_;
}

} // namespace Metavision
