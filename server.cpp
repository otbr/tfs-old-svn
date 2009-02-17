//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "server.h"
#include "connection.h"
#include "outputmessage.h"
#include "scheduler.h"

Server::Server(uint32_t serverip, uint16_t port):
m_io_service()
{
	m_acceptor = NULL;
	m_listenErrors = 0;
	m_shutdown = false;
	m_serverIp = serverip;
	m_serverPort = port;
	m_pendingStart = false;
	openListenSocket();
}

Server::~Server()
{
	closeListenSocket();
}

void Server::accept()
{
	if(m_shutdown || !m_acceptor)
		return;

	Connection* connection = ConnectionManager::getInstance()->createConnection(m_io_service);
	if(connection)
		m_acceptor->async_accept(connection->getHandle(), boost::bind(&Server::onAccept, this, connection, boost::asio::placeholders::error));
}

void Server::closeListenSocket()
{
	if(m_acceptor)
	{
		if(m_acceptor->is_open())
		{
			boost::system::error_code error;
			m_acceptor->close(error);
			if(error)
				PRINT_ASIO_ERROR("Closing listen socket");
		}

		delete m_acceptor;
		m_acceptor = NULL;
	}
}

void Server::openListenSocket()
{
	closeListenSocket();

	m_acceptor = new boost::asio::ip::tcp::acceptor(m_io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address(
		boost::asio::ip::address_v4(m_serverIp)), m_serverPort));
	accept();

	m_pendingStart = false;
}

void Server::onAccept(Connection* connection, const boost::system::error_code& error)
{
	if(!error)
	{
		#ifdef __DEBUG_NET_DETAIL__
		std::cout << "[Notice - Server::onAccept] Accepted connection." << std::endl;
		#endif
		connection->acceptConnection();
		accept();
	}
	else if(error != boost::asio::error::operation_aborted)
	{
		PRINT_ASIO_ERROR("Accepting");
		if(!m_pendingStart)
		{
			m_pendingStart = true;
			Scheduler::getScheduler().addEvent(createSchedulerTask(5000,
				boost::bind(&Server::openListenSocket, this)));
		}

		if(m_listenErrors > 100)
		{
			#ifndef __ENABLE_LISTEN_ERROR__
			std::cout << "[Warning - Server::onAccept] More than 100 listen errors." << std::endl;
			m_listenErrors = 0;
			#else
			std::cout << "[Error - Server::onAccept] More than 100 listen errors." << std::endl;
			return;
			#endif
		}

		m_listenErrors++;
	}
	#ifdef __DEBUG_NET__
	else
		std::cout << "[Error - Server::onAccept] Operation aborted." << std::endl;
	#endif
}

void Server::stop()
{
	m_shutdown = true;
	OutputMessagePool::getInstance()->stop();
	m_io_service.post(boost::bind(&Server::onStopServer, this));
}

void Server::onStopServer()
{
	closeListenSocket();
	//ConnectionManager::getInstance()->closeAll();
}
