#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <typeinfo>
#include <sstream>
using namespace std;

typedef pair<string, string> hostname_path;
typedef pair<hostname_path, int> hostname_path_port;
const string DELIMITER = "\r\n";

hostname_path_port parse(string url){
    // extract http part
    int hostname_start_pos = 0;
    string http_str = url.substr(0, 7);// 7 is for "http://"
    if(http_str.compare("http://") == 0){
        hostname_start_pos = 7;
    }
    string https_str = url.substr(0, 8); // 8 is for "https://"
    if(https_str.compare("https://") == 0){
        hostname_start_pos = 8;
    }
    // extract port if it exists
    int port_start_pos = -1, port = -1, port_end_pos = -1;
    for(int i = hostname_start_pos; i < (int)url.length(); i ++){
        if(url[i] == '/'){
            break;
        }
        if(url[i] == ':'){ // maybe port exists
            port_start_pos = i + 1;
            for(int j = i + 1; j < (int)url.length(); j ++){
                if(url[j] == '/'){
                    break;
                }
                port_end_pos = j;
            }
            if(port_end_pos >= port_start_pos){
                string port_str = url.substr(port_start_pos, 1 + port_end_pos - port_start_pos);
                try{
                    port = stoi(port_str);
                } catch (...){
                    cerr << "invalid url:" << endl;
                    exit(-1);
                }
            }
            break;
        }
    }
    if(port == -1){
        port_start_pos = -1;
        port_end_pos = -1;
    }
    // extract path
    int path_start_pos;
    for(path_start_pos = max(hostname_start_pos, port_end_pos); path_start_pos < (int)url.length(); path_start_pos ++){
        if(url[path_start_pos] == '/') {
            break;
        }
    }
    string path = "/";
    if(path_start_pos >= 0 && path_start_pos < (int)url.length() && url[path_start_pos] == '/'){
        path += url.substr(path_start_pos + 1, url.length() - 1 - path_start_pos);
    }
    // extract hostname
    int hostname_end_pos = min(path_start_pos, (int)(url.length()) - 1);
    if(port != -1){
        hostname_end_pos = port_start_pos - 2;
    }
    if(hostname_end_pos >= 0 && hostname_end_pos < (int)url.length() && url[hostname_end_pos] == '/'){
        hostname_end_pos --;
    }
    string hostname = url.substr(hostname_start_pos, hostname_end_pos - hostname_start_pos + 1);

    hostname_path_port res;
    hostname_path part1;
    part1.first = hostname;
    part1.second = path;
    res.first = part1;
    if(port != -1){
        res.second = port;
    } else {
        res.second = 80;
    }
    return res;
}

int send_msg(int sockFD, string msg){
    int nbytes_total = 0;
    const char* request = msg.c_str();
    while(nbytes_total < (int)msg.length()){
        int nbytes_last = send(sockFD, request + nbytes_total, msg.length() - nbytes_total, 0);
        if(nbytes_last == -1){
            cerr << "fail to send msg back" << gai_strerror(nbytes_last) << endl;
            return nbytes_last;
        }
        nbytes_total += nbytes_last;
    }
    return 0;
}

int find_delimiter(char* s, int len, int start_pos){
    for(int i = start_pos; i < len - 1; i ++){
        if(s[i] == '\r' && s[i + 1] == '\n'){
            return i;
        }
    }
    return -1;
}

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr << "Usage:./http_cli URL" << endl;
        return EXIT_FAILURE;
    }

    string url = argv[1];
    hostname_path_port res = parse(url);
    // cout << "hostname:" << res.first.first << endl;
    // cout << "path:" << res.first.second << endl;
    // cout << "port:" << res.second << endl;
    string hostname = res.first.first;
    string path = res.first.second;
    int port = res.second;

    int error, sockFD;
    struct addrinfo hints, *addrs, *it;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if((error = getaddrinfo(hostname.c_str(), to_string(port).c_str(), &hints, &addrs)) != 0){
        cerr << "fail to get addr[" << hostname << "] info, " << gai_strerror(error) << endl;
        return EXIT_FAILURE;
    }

    for(it = addrs; it != NULL; it = it->ai_next){
        if((sockFD = socket(it->ai_family, it->ai_socktype, it->ai_protocol)) == -1){
            cerr << "fail to create socket, " << gai_strerror(sockFD) << endl;
            continue;
        }
        if((error = connect(sockFD, it->ai_addr, it->ai_addrlen)) == -1){
            close(sockFD);
            cerr << "fail to connect, " << gai_strerror(error) << endl;
            continue;
        }
        //successfully
        break;
    }

    if(it == NULL){
        cerr << "fail to connect to the server[" << hostname << "]" << endl;
        return EXIT_FAILURE;
    }

    // prepare the request
    stringstream reqs;
    reqs << "GET " << path << " " << "HTTP/1.1" << DELIMITER;
    reqs << "Host: " << hostname << DELIMITER;
    reqs << "Connection: close" << DELIMITER;
    reqs << DELIMITER;

    cerr << reqs.str() << endl;
    if(send_msg(sockFD, reqs.str()) == -1){
        cerr << "send data to server failed" << endl;
        return EXIT_FAILURE;
    }

    char buffer[100];
    int resp_cap = 1000, resp_len = 0;
    char *resp = new char[resp_cap];
    while(true){
        int size = recv(sockFD, buffer, 100, 0);
        if(size == -1){
            cerr << "recv data failed" << endl;
            return EXIT_FAILURE;
        }
        if(size == 0){
            break;
        }
        if(size > (resp_cap - resp_len)){
            int new_resp_cap = resp_cap + 1000;
            char* new_resp = new char[new_resp_cap];
            memset(new_resp, 0, new_resp_cap);
            memcpy(new_resp, resp, resp_len);
            delete[] resp;
            resp = new_resp;
            resp_cap = new_resp_cap;
        }
        memcpy(&resp[resp_len], buffer, size);
        memset(buffer, 0, sizeof(buffer));
        resp_len += size;
    }

    // parse start line
    int start_line_pos = find_delimiter(resp, resp_len, 0);
    if(start_line_pos == -1){
        return EXIT_FAILURE;
    }
    char* start_line = new char[start_line_pos + 1];
    memcpy(start_line, resp, start_line_pos);
    start_line[start_line_pos] = '\0';
    cerr << start_line << endl;
    start_line_pos += 2;
    // parse header line
    int head_line_pos;
    while((head_line_pos = find_delimiter(resp, resp_len, start_line_pos)) != -1){
        if(head_line_pos == -1){
            return EXIT_FAILURE;
        }
        if(head_line_pos == start_line_pos){
            start_line_pos += 2;
            break;
        }
        char* header_line = new char[head_line_pos - start_line_pos + 1];
        memcpy(header_line, resp + start_line_pos, head_line_pos - start_line_pos);
        header_line[head_line_pos - start_line_pos] = '\0';
        cerr << header_line << endl;
        start_line_pos = head_line_pos + 2;
    }
    // parse body
    char* body = new char[resp_len - start_line_pos];
    memcpy(body, resp + start_line_pos, resp_len - start_line_pos);
    cout.write(body, resp_len - start_line_pos);
    return 0;
}