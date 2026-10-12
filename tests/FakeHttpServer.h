#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <utility>

/// A loopback HTTP/1.1 server that records each request and answers with canned
/// responses. Just enough for an OAuth or REST client.
class FakeHttpServer
{
public:
    struct Request
    {
        QByteArray method;
        QByteArray target;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    FakeHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                serve(socket);
        });
    }

    [[nodiscard]] QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    /// The response for every request not answered by the queue.
    void respond(int status, QByteArray json)
    {
        m_status = status;
        m_body = std::move(json);
    }

    /// A response for one request, used in order before falling back to respond().
    void enqueue(int status, QByteArray json) { m_queue.append({status, std::move(json)}); }

    using Response = std::pair<int, QByteArray>;

    /// Answers every request instead of the queue and respond(), for requests
    /// that arrive in no fixed order.
    std::function<Response(const Request &)> handler;

    /// A response with headers of its own, such as ETag.
    struct Reply
    {
        int status = 200;
        QByteArray body;
        QList<std::pair<QByteArray, QByteArray>> headers = {};
        QByteArray contentType = "application/json";
    };
    /// Answers every request, ahead of handler.
    std::function<Reply(const Request &)> replier;

    QList<Request> requests;

private:
    void serve(QTcpSocket *socket)
    {
        auto *buffer = new QByteArray;
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer] {
            delete buffer;
            socket->deleteLater();
        });
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;

            Request request;
            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            request.method = requestLine.value(0);
            request.target = requestLine.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines[i].indexOf(':');
                if (colon > 0)
                    request.headers.insert(lines[i].left(colon).trimmed().toLower(),
                                           lines[i].mid(colon + 1).trimmed());
            }
            const qsizetype length = request.headers.value("content-length", "0").toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            request.body = buffer->mid(headerEnd + 4, length);
            requests.append(request);

            Reply reply;
            if (replier) {
                reply = replier(request);
            } else {
                const auto [status, body] = handler             ? handler(request)
                                            : m_queue.isEmpty() ? Response(m_status, m_body)
                                                                : m_queue.takeFirst();
                reply.status = status;
                reply.body = body;
            }
            QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) +
                              " X\r\nContent-Type: " + reply.contentType + "\r\n";
            for (const auto &[name, value] : std::as_const(reply.headers))
                head += name + ": " + value + "\r\n";
            socket->write(head + "Content-Length: " + QByteArray::number(reply.body.size()) +
                          "\r\nConnection: close\r\n\r\n" + reply.body);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QList<std::pair<int, QByteArray>> m_queue;
    int m_status = 200;
    QByteArray m_body = "{}";
};
