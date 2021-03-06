/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#pragma once

#include <uv.h>

#include <memory>

#include "TcpServer.h"

#include <tbb/concurrent_queue.h>

namespace net
{
class UvTcpServer;

class UvTcpServerStream : public TcpServerStream
{
private:
	UvTcpServer* m_server;

	std::unique_ptr<uv_tcp_t> m_client;

	std::unique_ptr<uv_async_t> m_writeCallback;

	tbb::concurrent_queue<std::function<void()>> m_pendingRequests;

	std::vector<char> m_readBuffer;

private:
	void HandleRead(ssize_t nread, const uv_buf_t* buf);

	void HandlePendingWrites();

	void CloseClient();

	inline std::vector<char>& GetReadBuffer()
	{
		return m_readBuffer;
	}

public:
	UvTcpServerStream(UvTcpServer* server);

	virtual ~UvTcpServerStream();

	bool Accept(std::unique_ptr<uv_tcp_t>&& client);

	virtual void AddRef() override
	{
		TcpServerStream::AddRef();
	}

	virtual bool Release() override
	{
		return TcpServerStream::Release();
	}

public:
	virtual PeerAddress GetPeerAddress() override;

	virtual void Write(const std::vector<uint8_t>& data) override;

	virtual void Close() override;
};

class TcpServerManager;

class UvTcpServer : public TcpServer
{
private:
	TcpServerManager* m_manager;

	std::unique_ptr<uv_tcp_t> m_server;

	std::set<fwRefContainer<UvTcpServerStream>> m_clients;

private:
	void OnConnection(int status);

public:
	UvTcpServer(TcpServerManager* manager);

	virtual ~UvTcpServer();

	bool Listen(std::unique_ptr<uv_tcp_t>&& server);

	inline uv_tcp_t* GetServer()
	{
		return m_server.get();
	}

	inline TcpServerManager* GetManager()
	{
		return m_manager;
	}

public:
	void RemoveStream(UvTcpServerStream* stream);
};
}

// helpful wrappers
class UvClosable
{
public:
	virtual ~UvClosable() = default;
};

class UvVirtualBase
{
public:
	virtual ~UvVirtualBase() = 0;
};

template<typename Handle, typename TFn>
auto UvCallbackWrap(Handle* handle, const TFn& fn)
{
	struct Request : public UvClosable
	{
		TFn fn;

		Request(const TFn& fn)
			: fn(fn)
		{

		}

		static void cb(Handle* handle)
		{
			Request* request = reinterpret_cast<Request*>(handle->data);

			request->fn(handle);
			delete request;
		}
	};

	auto req = new Request(fn);
	handle->data = req;

	return &Request::cb;
}

template<typename... TArgs>
struct UvCallbackArgs
{
	template<typename Handle, typename TFn>
	static auto Get(Handle* handle, TFn fn)
	{
		struct Request : public UvClosable
		{
			TFn fn;

			Request(TFn fn)
				: fn(std::move(fn))
			{

			}

			static void cb(Handle* handle, TArgs... args)
			{
				Request* request = reinterpret_cast<Request*>(handle->data);

				request->fn(handle, args...);
				delete request;
			}
		};

		auto req = new Request(std::move(fn));
		handle->data = req;

		return &Request::cb;
	}
};

template<typename Handle, typename TFn>
auto UvPersistentCallback(Handle* handle, TFn fn)
{
	struct Request : public UvClosable
	{
		TFn fn;

		Request(TFn fn)
			: fn(std::move(fn))
		{

		}

		static void cb(Handle* handle)
		{
			Request* request = reinterpret_cast<Request*>(handle->data);

			request->fn(handle);
		}
	};

	auto req = new Request(std::move(fn));
	handle->data = req;

	return &Request::cb;
}

// generic wrapper for libuv closing
template<typename Handle, typename TFn>
void UvCloseHelper(std::unique_ptr<Handle> handle, const TFn& fn)
{
	struct TempCloseData
	{
		std::unique_ptr<Handle> item;
		UvClosable* closable;
	};

	// create temporary object and give it our reference
	TempCloseData* tempCloseData = new TempCloseData;
	tempCloseData->closable = nullptr;

	try
	{
		if (handle->data)
		{
			auto closable = dynamic_cast<UvClosable*>(static_cast<UvVirtualBase*>(handle->data));

			if (closable)
			{
				tempCloseData->closable = closable;
			}
		}
	}
	catch (std::exception&)
	{

	}

	tempCloseData->item = std::move(handle);
	tempCloseData->item->data = tempCloseData;

	fn(tempCloseData->item.get(), [](auto* handle)
	{
		auto closeData = reinterpret_cast<TempCloseData*>(handle->data);

		// delete the closable, if any
		delete closeData->closable;

		// delete the close holder
		delete closeData;
	});
}

// wrapper to make sure the libuv handle only gets freed after the close completes
template<typename Handle>
void UvClose(std::unique_ptr<Handle> handle)
{
	return UvCloseHelper(std::move(handle), [](auto handle, auto cb)
	{
		uv_close((uv_handle_t*)handle, cb);
	});
}

template<typename T>
class UvHandleContainer
{
public:
	UvHandleContainer()
	{
		m_handle = std::make_unique<T>();
	}

	UvHandleContainer(UvHandleContainer&& right)
	{
		m_handle = std::move(right.m_handle);
	}

	~UvHandleContainer()
	{
		if (m_handle)
		{
			UvClose(std::move(m_handle));
		}
	}

	inline T* get()
	{
		return m_handle.get();
	}

	inline T* operator&()
	{
		return m_handle.get();
	}

private:
	std::unique_ptr<T> m_handle;
};
