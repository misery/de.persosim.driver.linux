#include "persoSimConnect.h"
#include "hexString.h"

#include <string.h>
#include <debuglog.h>
#include <ifdhandler.h>
#include <pcsclite.h>

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <limits.h>

// socket file descriptor for the connection to the client
int clientSocket = -1;

// internal non-exported helper functions
int transmit (DWORD lun, const char* msgBuffer) {
	//TODO make this function lun-aware
	//if no client connection is available return immediately
	if (clientSocket < 0) {
		return PSIM_NO_CONNECTION;
	}

	int bufferLength = strlen(msgBuffer) +1 ;
	char transmitBuffer[bufferLength];
	strcpy(transmitBuffer, msgBuffer);
	strcat(transmitBuffer, "\n");
	Log2(PCSC_LOG_DEBUG, "Sending message to client: %s", transmitBuffer);

	if (send(clientSocket, transmitBuffer, bufferLength, MSG_NOSIGNAL) < 0) {
			Log1(PCSC_LOG_ERROR, "Failure during transmit to client");
			return PSIM_COMMUNICATION_ERROR;
	}
	return PSIM_SUCCESS;
}

int receive(DWORD lun, char* response, int respLength) {
	//TODO make this function lun-aware
	//if no client connection is available return immediately
	if (clientSocket < 0) {
		return PSIM_NO_CONNECTION;
	}

	int len = 0;
	do {
		len += recv(clientSocket, response + len, respLength - len, 0);
		// if len == 0 after the first loop the connection was closed
	} while (len > 0 && len < respLength && response[len-1] != '\n');

	if (len > 0) {
		response[len-1] = '\0';
	} else {
		Log1(PCSC_LOG_ERROR, "Failure during receive from client (no response received)");
		return PSIM_COMMUNICATION_ERROR;
	}

	return PSIM_SUCCESS;
}

int exchangePcscFunction(const char* function, DWORD lun, const char* params, char * response, int respLength) {
	//TODO make this function lun-aware

	const int minLength = 6; // 2*function + divider + 2*lun + \0
	const int paramLength = strlen(params);
	const int bufferLength = (paramLength) ?  minLength + 1 + paramLength : minLength;
	char msgBuffer[bufferLength];
	strcpy(msgBuffer, PSIM_MSG_FUNCTION_IS_ICC_PRESENT);
	strcat(msgBuffer, PSIM_MSG_DIVIDER);
	HexInt2String(lun, &msgBuffer[3]);
	msgBuffer[bufferLength-1] = '\0';
	if (bufferLength > minLength) {
		strcat(msgBuffer, PSIM_MSG_DIVIDER);
		strcat(msgBuffer, params);
	}

	Log2(PCSC_LOG_DEBUG, "PSIM transmit PCSC function: %s", msgBuffer);

	int retVal = transmit(lun, msgBuffer);
	if (retVal != PSIM_SUCCESS) {
		Log1(PCSC_LOG_ERROR, "Could not transmit PCSC function to PersoSim Connector");
		return retVal;
	}

	receive(lun, response, respLength);
	if (retVal != PSIM_SUCCESS) {
		Log1(PCSC_LOG_ERROR, "Could not receive PCSC response from PersoSim Connector");
		return retVal;
	}

	return PSIM_SUCCESS;

}

RESPONSECODE extractPcscResponseCode(const char* response) {
	char responseCodeString[9];
	strncpy(responseCodeString, response, 8);

	return HexString2Int(responseCodeString);
}

int PSIMStartHandshakeServer(int port)
{
	//XXX put this off into another thread
	//allocate handshake socket
	int handshakeSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (handshakeSocket < 0)
	{
		Log2(PCSC_LOG_ERROR, "Unable to allocate server socket for handshake (port %d)", port);
		return PSIM_COMMUNICATION_ERROR;
	}

	struct sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(port);

	// bind handshake socket
	if (bind(handshakeSocket, (struct sockaddr *) &serverAddr,
			sizeof(serverAddr)) < 0)
	{
		Log2(PCSC_LOG_ERROR,
				"Unable to bind server socket for handshake to port %d",
				port);
		return PSIM_COMMUNICATION_ERROR;
	}

	if (listen(handshakeSocket, 5) < 0)
	{
		if (errno == EADDRINUSE) {
			Log1(PCSC_LOG_ERROR, "Handshake socket already in use");
		} else {
			Log1(PCSC_LOG_ERROR, "Unable to listen on handshake socket");
		}
		return PSIM_COMMUNICATION_ERROR;
	}

	//accept client connection
	clientSocket = accept(handshakeSocket, NULL, NULL);
	if (clientSocket < 0) {
		Log1(PCSC_LOG_ERROR, "Handshake socket failed to accept client connection");
		return PSIM_COMMUNICATION_ERROR;
	}


	//hardcoded handshake
	char handshakeLun = 0xff; //TODO handle this special case when making code lun aware
	int msgBufferSize = 256;
	char msgBuffer[msgBufferSize];
	int rv = receive(handshakeLun, msgBuffer, msgBufferSize);
	if (rv != PSIM_SUCCESS) {
		Log1(PCSC_LOG_ERROR, "Client did not initiate a valid handshake");
		return PSIM_COMMUNICATION_ERROR;
	}
	Log2(PCSC_LOG_DEBUG, "Client initiated handshake: %s\n", msgBuffer);

	strcpy(msgBuffer, PSIM_MSG_HANDSHAKE_IFD_HELLO);
	strcat(msgBuffer, PSIM_MSG_DIVIDER);
	strcat(msgBuffer, "00"); //TODO hardcoded LUN
	strcat(msgBuffer, "\n");
	rv = transmit(handshakeLun, msgBuffer);
	if (rv != PSIM_SUCCESS) {
		Log1(PCSC_LOG_ERROR, "Could not send response to handshake initialization");
		return PSIM_COMMUNICATION_ERROR;
	}

	rv = receive(handshakeLun, msgBuffer, msgBufferSize);
	if (rv != PSIM_SUCCESS) {
		Log1(PCSC_LOG_ERROR, "Client did not correctly finish handshake");
		return PSIM_COMMUNICATION_ERROR;
	}
	Log2(PCSC_LOG_DEBUG, "Client finished handshake: %s\n", msgBuffer);

	//TODO implement closing and disposal of client sockets (through handshake as well as on errors)

	return PSIM_SUCCESS;
}

int PSIMIsIccPresent(DWORD lun) {
	//prepare params and response
	char params[1];
	params[0] = '\0';
	char respBufferSize = 9;
	char response[respBufferSize];

	//forward pcsc function to client
	int rv = exchangePcscFunction(PSIM_MSG_FUNCTION_IS_ICC_PRESENT, lun, PSIM_MSG_EMPTY_PARAMS, response, respBufferSize);
	if (rv != PSIM_SUCCESS) {
		return IFD_ICC_NOT_PRESENT;
	}

	//return the response code
	return extractPcscResponseCode(response);
}

//TODO remove old code below
int PSIMCloseConnection()
{
	//close socket connection
	if (clientSocket >= 0)
	{
		if (close(clientSocket) != 0)
		{
			Log2(PCSC_LOG_ERROR, "Closing socket failed with errno %d", errno);
			return PSIM_COMMUNICATION_ERROR;
		}
	}

	//store connection state
	clientSocket = -1;
	Log1(PCSC_LOG_DEBUG, "Socket disconnected");

	return PSIM_SUCCESS;
}

/*
 * Expects cmdApdu to contain a HexString and respApdu to be large enough to hold the complete response HexString both including \0 termination
 */
void exchangeApdu(const char* cmdApdu, char* respApdu, int respApduSize)
{
	return;
	//Log2(PCSC_LOG_DEBUG, "exchangeApdu command APDU\n%s\n", cmdApdu);

	//ignore SIGPIPE for this function
	struct sigaction ignAct, oldAct;
	memset(&ignAct, '\0', sizeof(ignAct));
	ignAct.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ignAct, &oldAct);

	// transmit cmdApdu (ignoring errors, missing response will be handled later on)
	send(clientSocket, cmdApdu, strlen(cmdApdu), 0); 
	send(clientSocket, "\n", 1, 0); 

	// receive response
	int len = 0;
	do {
		len += recv(clientSocket, respApdu + len, respApduSize - len, 0);
		// if len == 0 after the first loop the connection was closed
	} while (len > 0 && len < respApduSize && respApdu[len-1] != '\n');

	if (len > 4) {
		respApdu[len-1] = '\0';
	}
	else
	{
		// no valid response was received (did not even contain SW)
		// include fake SW
		strcpy(respApdu, "6FFF");
	}

	//Log2(PCSC_LOG_DEBUG, "exchangeApdu response APDU\n%s\n", respApdu);
	
	// reset handler for SIGPIPE
	sigaction(SIGPIPE, &oldAct, NULL);
}

int PSIMIsConnected()
{
	return clientSocket >= 0;
}
