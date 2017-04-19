/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/messagequeue.hh"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <maxscale/debug.h>
#include <maxscale/log_manager.h>
#include "maxscale/worker.hh"

namespace maxscale
{

MessageQueue::MessageQueue(Handler* pHandler, int read_fd, int write_fd)
    : MxsPollData(&MessageQueue::poll_handler)
    , m_handler(*pHandler)
    , m_read_fd(read_fd)
    , m_write_fd(write_fd)
    , m_pWorker(NULL)
{
    ss_dassert(pHandler);
    ss_dassert(read_fd);
    ss_dassert(write_fd);
}

MessageQueue::~MessageQueue()
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
    }

    close(m_read_fd);
    close(m_write_fd);
}

//static
MessageQueue* MessageQueue::create(Handler* pHandler)
{
    MessageQueue* pThis = NULL;

    // We create the pipe in message mode (O_DIRECT), so that we do
    // not need to deal with partial messages and as non blocking so
    // that the descriptor can be added to an epoll instance.

    int fds[2];
    if (pipe2(fds, O_DIRECT | O_NONBLOCK | O_CLOEXEC) == 0)
    {
        int read_fd = fds[0];
        int write_fd = fds[1];

        pThis = new (std::nothrow) MessageQueue(pHandler, read_fd, write_fd);

        if (!pThis)
        {
            MXS_OOM();
            close(read_fd);
            close(write_fd);
        }
    }
    else
    {
        MXS_ERROR("Could not create pipe for worker: %s", mxs_strerror(errno));
    }

    return pThis;
}

bool MessageQueue::post(const Message& message) const
{
    // NOTE: No logging here, this function must be signal safe.
    bool rv = false;

    ss_dassert(m_pWorker);
    if (m_pWorker)
    {
        ssize_t n = write(m_write_fd, &message, sizeof(message));
        rv = (n == sizeof(message));
    }
    else
    {
        MXS_ERROR("Attempt to post using a message queue that is not added to a worker.");
    }

    return rv;
}

bool MessageQueue::add_to_worker(Worker* pWorker)
{
    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
        m_pWorker = NULL;
    }

    if (pWorker->add_fd(m_read_fd, EPOLLIN, this))
    {
        m_pWorker = pWorker;
    }

    return m_pWorker != NULL;
}

Worker* MessageQueue::remove_from_worker()
{
    Worker* pWorker = m_pWorker;

    if (m_pWorker)
    {
        m_pWorker->remove_fd(m_read_fd);
        m_pWorker = NULL;
    }

    return pWorker;
}

uint32_t MessageQueue::handle_poll_events(int thread_id, uint32_t events)
{
    uint32_t rc = MXS_POLL_NOP;

    // We only expect EPOLLIN events.
    ss_dassert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    if (events & EPOLLIN)
    {
        Message message;

        ssize_t n;

        do
        {
            n = read(m_read_fd, &message, sizeof(message));

            if (n == sizeof(message))
            {
                m_handler.handle_message(*this, message);
            }
            else if (n == -1)
            {
                if (errno != EWOULDBLOCK)
                {
                    MXS_ERROR("Worker could not read from pipe: %s", mxs_strerror(errno));
                }
            }
            else if (n != 0)
            {
                // This really should not happen as the pipe is in message mode. We
                // should either get a message, nothing at all or an error. In non-debug
                // mode we continue reading in order to empty the pipe as otherwise the
                // thread may hang.
                MXS_ERROR("MessageQueue could only read %ld bytes from pipe, although "
                          "expected %lu bytes.", n, sizeof(message));
                ss_dassert(!true);
            }
        }
        while ((n != 0) && (n != -1));

        rc = MXS_POLL_READ;
    }

    return rc;
}

//static
uint32_t MessageQueue::poll_handler(MXS_POLL_DATA* pData, int thread_id, uint32_t events)
{
    MessageQueue* pThis = static_cast<MessageQueue*>(pData);

    return pThis->handle_poll_events(thread_id, events);
}

}
