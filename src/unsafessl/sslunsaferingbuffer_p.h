/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef SSLUNSAFERINGBUFFER_P_H
#define SSLUNSAFERINGBUFFER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of a number of Qt sources files.  This header file may change from
// version to version without notice, or even be removed.
//
// We mean it.
//

//#include <QtCore/private/qglobal_p.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qvector.h>

QT_BEGIN_NAMESPACE

#ifndef QRINGBUFFER_CHUNKSIZE
#define QRINGBUFFER_CHUNKSIZE 4096
#endif

class SslUnsafeRingChunk
{
public:
    // initialization and cleanup
    inline SslUnsafeRingChunk() Q_DECL_NOTHROW :
        headOffset(0), tailOffset(0)
    {
    }
    inline SslUnsafeRingChunk(const SslUnsafeRingChunk &other) Q_DECL_NOTHROW :
        chunk(other.chunk), headOffset(other.headOffset), tailOffset(other.tailOffset)
    {
    }
    explicit inline SslUnsafeRingChunk(int alloc) :
        chunk(alloc, Qt::Uninitialized), headOffset(0), tailOffset(0)
    {
    }
    explicit inline SslUnsafeRingChunk(const QByteArray &qba) Q_DECL_NOTHROW :
        chunk(qba), headOffset(0), tailOffset(qba.size())
    {
    }

    inline SslUnsafeRingChunk &operator=(const SslUnsafeRingChunk &other) Q_DECL_NOTHROW
    {
        chunk = other.chunk;
        headOffset = other.headOffset;
        tailOffset = other.tailOffset;
        return *this;
    }
    inline SslUnsafeRingChunk(SslUnsafeRingChunk &&other) Q_DECL_NOTHROW :
        chunk(other.chunk), headOffset(other.headOffset), tailOffset(other.tailOffset)
    {
        other.headOffset = other.tailOffset = 0;
    }
    inline SslUnsafeRingChunk &operator=(SslUnsafeRingChunk &&other) Q_DECL_NOTHROW
    {
        swap(other);
        return *this;
    }

    inline void swap(SslUnsafeRingChunk &other) Q_DECL_NOTHROW
    {
        chunk.swap(other.chunk);
        qSwap(headOffset, other.headOffset);
        qSwap(tailOffset, other.tailOffset);
    }

    // allocating and sharing
    void allocate(int alloc);
    inline bool isShared() const
    {
        return !chunk.isDetached();
    }
    Q_CORE_EXPORT void detach();
    QByteArray toByteArray();

    // getters
    inline int head() const
    {
        return headOffset;
    }
    inline int size() const
    {
        return tailOffset - headOffset;
    }
    inline int capacity() const
    {
        return chunk.size();
    }
    inline int available() const
    {
        return chunk.size() - tailOffset;
    }
    inline const char *data() const
    {
        return chunk.constData() + headOffset;
    }
    inline char *data()
    {
        if (isShared())
            detach();
        return chunk.data() + headOffset;
    }

    // array management
    inline void advance(int offset)
    {
        Q_ASSERT(headOffset + offset >= 0);
        Q_ASSERT(size() - offset > 0);

        headOffset += offset;
    }
    inline void grow(int offset)
    {
        Q_ASSERT(size() + offset > 0);
        Q_ASSERT(head() + size() + offset <= capacity());

        tailOffset += offset;
    }
    inline void assign(const QByteArray &qba)
    {
        chunk = qba;
        headOffset = 0;
        tailOffset = qba.size();
    }
    inline void reset()
    {
        headOffset = tailOffset = 0;
    }
    inline void clear()
    {
        assign(QByteArray());
    }

private:
    QByteArray chunk;
    int headOffset, tailOffset;
};

class SslUnsafeRingBuffer
{
public:
    explicit inline SslUnsafeRingBuffer(int growth = QRINGBUFFER_CHUNKSIZE) :
        bufferSize(0), basicBlockSize(growth) { }

    inline void setChunkSize(int size) {
        basicBlockSize = size;
    }

    inline int chunkSize() const {
        return basicBlockSize;
    }

    inline qint64 nextDataBlockSize() const {
        return bufferSize == 0 ? Q_INT64_C(0) : buffers.first().size();
    }

    inline const char *readPointer() const {
        return bufferSize == 0 ? nullptr : buffers.first().data();
    }

    const char *readPointerAtPosition(qint64 pos, qint64 &length) const;
    void free(qint64 bytes);
    char *reserve(qint64 bytes);
    char *reserveFront(qint64 bytes);

    inline void truncate(qint64 pos) {
        Q_ASSERT(pos >= 0 && pos <= size());

        chop(size() - pos);
    }

    void chop(qint64 bytes);

    inline bool isEmpty() const {
        return bufferSize == 0;
    }

    inline int getChar() {
        if (isEmpty())
            return -1;
        char c = *readPointer();
        free(1);
        return int(uchar(c));
    }

    inline void putChar(char c) {
        char *ptr = reserve(1);
        *ptr = c;
    }

    void ungetChar(char c)
    {
        char *ptr = reserveFront(1);
        *ptr = c;
    }


    inline qint64 size() const {
        return bufferSize;
    }

    void clear();
    inline qint64 indexOf(char c) const { return indexOf(c, size()); }
    qint64 indexOf(char c, qint64 maxLength, qint64 pos = 0) const;
    qint64 read(char *data, qint64 maxLength);
    QByteArray read();
    qint64 peek(char *data, qint64 maxLength, qint64 pos = 0) const;
    void append(const char *data, qint64 size);
    void append(const QByteArray &qba);

    inline qint64 skip(qint64 length) {
        qint64 bytesToSkip = qMin(length, bufferSize);

        free(bytesToSkip);
        return bytesToSkip;
    }

    qint64 readLine(char *data, qint64 maxLength);

    inline bool canReadLine() const {
        return indexOf('\n') >= 0;
    }

private:
    enum {
        // Define as enum to force inlining. Don't expose MaxAllocSize in a public header.
        MaxByteArraySize = INT_MAX - sizeof(std::remove_pointer<QByteArray::DataPtr>::type)
    };

    QVector<SslUnsafeRingChunk> buffers;
    qint64 bufferSize;
    int basicBlockSize;
};

Q_DECLARE_SHARED(SslUnsafeRingChunk)
Q_DECLARE_TYPEINFO(SslUnsafeRingBuffer, Q_MOVABLE_TYPE);

QT_END_NAMESPACE

#endif // SSLUNSAFERINGBUFFER_P_H
