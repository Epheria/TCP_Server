#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <iostream>
#include <vector>
#include <algorithm>

#define MAX_BUFFER_SIZE 2048

void err_quit(const char* msg);
DWORD WINAPI ProcessClient(LPVOID arg);

HANDLE g_hMutex;
std::vector<SOCKET> ClientList;

// 패킷의 형태가 여러개이므로 pragma pack 안에 넣으면됨
#pragma pack(1)
struct Packet
{
	char buf[256];
};
#pragma pack()

int main()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
	MessageBoxA(NULL, "윈속을 사용할 준비가 되었다.", "윈속 사용 준비 완료.", MB_OK);

	// 뮤텍스 생성
	g_hMutex = CreateMutex(NULL, false, NULL);
	if (NULL == g_hMutex) return -1;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(g_hMutex);
		return -1;
	}

	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == listen_socket) exit(-1);

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(9000);
	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	//bind() 소켓 설정
	if (bind(listen_socket, (SOCKADDR*)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR)
	{
		closesocket(listen_socket);
		WSACleanup();
		err_quit("bind");
	}

	// listen() 수신 대기열 생성
	if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
	{
		closesocket(listen_socket);
		WSACleanup();
		err_quit("listen");
	}

	// 데이터 통신에 사용할 변수
	SOCKADDR_IN clientaddr;
	int addrlen = sizeof(SOCKADDR_IN);
	ZeroMemory(&clientaddr, addrlen);

	SOCKET client_socket;
	HANDLE hThread;
	DWORD threadID;
	int retval;
	char buf[MAX_BUFFER_SIZE + 1];
	while (1)
	{
		//accept() 연결 대기.
		client_socket = accept(listen_socket, (SOCKADDR*)&clientaddr, &addrlen);
		if (INVALID_SOCKET == client_socket) continue;

		printf("\n[TCP 서버] 클라이언트 접속 : IP 주소=%s, 포트 번호=%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

		WaitForSingleObject(g_hMutex, INFINITE);
		ClientList.push_back(client_socket);
		ReleaseMutex(g_hMutex);

		// 스레드 생성
		hThread = CreateThread(NULL, 0, ProcessClient, (LPVOID)client_socket, 0, &threadID);
		if (NULL == hThread) std::cout << "[오류] 스레드 생성 실패!" << std::endl;
		else CloseHandle(hThread);
	}

	CloseHandle(g_hMutex);
	closesocket(listen_socket);
	WSACleanup();
}

void err_quit(const char* msg)
{
	LPVOID IpMsgBuf;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&IpMsgBuf, 0, NULL);

	MessageBoxA(NULL, (LPCSTR)IpMsgBuf, msg, MB_ICONERROR);

	LocalFree(IpMsgBuf);
	exit(-1);
}

// 이 스레드 함수는 호출이 되는게 아니라 클라이언트가 추가 될 때마다
// 추가로 실행되는것임 즉 멀티스레드
DWORD WINAPI ProcessClient(LPVOID arg)
{
	SOCKET client_socket = (SOCKET)arg;
	SOCKADDR_IN clientaddr;
	int addrlen = sizeof(clientaddr);
	getpeername(client_socket, (SOCKADDR*)&clientaddr, &addrlen);
	int retval;
	char buf[MAX_BUFFER_SIZE + 1];

	while (1)
	{
		ZeroMemory(buf, sizeof(buf));
		// 데이터 받기
		retval = recv(client_socket, buf, sizeof(buf), 0);
		Packet* recv_packet = (Packet*)buf;
		if (SOCKET_ERROR == retval) break;
		else if (0 == retval) break;

		// 받은 데이터 출력
		buf[retval - 1] = '\0';
		printf("\n[TCP/%s:%d] %s\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), buf);

		Packet send_packet = *recv_packet;
		// 데이터 보내기
		WaitForSingleObject(g_hMutex, INFINITE);
		for (const auto& sock : ClientList)
		{
			retval = send(sock, (char*)&send_packet, sizeof(Packet), 0);
			if (SOCKET_ERROR == retval) break;
		}
		ReleaseMutex(g_hMutex);
	}

	WaitForSingleObject(g_hMutex, INFINITE);
	auto iter = std::find(ClientList.begin(), ClientList.end(), client_socket);
	if (ClientList.end() != iter)
	{
		ClientList.erase(iter);
	}
	ReleaseMutex(g_hMutex);

	// 클라이언트 소켓 종료
	closesocket(client_socket);

	printf("\n[TCP 서버] 클라이언트 종료 : IP 주소=%s, 포트 번호=%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

	return 0;
}