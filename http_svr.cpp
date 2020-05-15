#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <bits/stdc++.h> 
#include <filesystem>
using namespace std;


const int MAX_CONNS = 10;
const int BUFFERSIZE = 100;
const string DELIMITER = "\r\n";

// following consts are used in parse_req function denoting the status of parsing
const int NOT_COMPLETE_DATA = -1;
const int PARSE_REQ_SUCCESS = 0;

string req_path; // store the path in some req, if it exists
int method_typ; // 0 for unknown type(still waiting for being assigned), 1 for GET, -1 for other methods type
enum METHOD_TYPE{GET = 1, UNKNOWN = 0, OTHERS = -1};
const string OK = "200 OK";
const string BAD_REQUEST = "400 Bad Request";
const string NOT_IMPLEMENTED = "501 Not Implemented";
const string NOT_FOUND = "404 Not Found";
const string INTERNAL_ERROR = "500 Internal Server Error";


bool is_file(string path){
    for(int i = path.size() - 1; i >= 0; i --){
        if(path[i] == '/'){
            return false;
        }
        if(path[i] == '.'){
            return true;
        }
    }
    return false;
}

int find_delimiter(char* s, int len, int start_pos){
    for(int i = start_pos; i < len - 1; i ++){
        if(s[i] == '\r' && s[i + 1] == '\n'){
            return i;
        }
    }
    return -1;
}

bool is_get(string data){
    return data.compare("GET") == 0;
}

/**
 * send a message back to client
 * @param sockFD the sock fd of the the client
 * @param msg the message
 * @param msg_len the length of message
 */
int _send_msg(int sockFD, char* msg, int msg_len){
    cout << "response size:" << msg_len << endl;
    int nbytes_total = 0;
    while(nbytes_total < msg_len){
        int nbytes_last = send(sockFD, msg + nbytes_total, msg_len - nbytes_total, 0);
        if(nbytes_last == -1){
            cerr << "fail to send msg back" << gai_strerror(nbytes_last) << endl;
            delete msg;
            return nbytes_last;
        }
        nbytes_total += nbytes_last;
    }
    delete msg;
    return 0;
}

char* _build_response(string status_code, vector<string> *headers, char* msg, int &msg_len){
    int cap = 1000, len = 0;
    char* res = new char[cap];
    //build start line
    string start_line = "HTTP/1.1 " + status_code + DELIMITER;
    for(int i = 0; i < (int)start_line.length(); i ++){
        if(len == cap){
            int new_cap = cap + 1000;
            char* new_buffer = new char[new_cap];
            memcpy(new_buffer, res, len);
            delete res;
            res = new_buffer;
            cap = new_cap;
        }
        res[len ++] = start_line[i];
    }
    int size1 = len;
    cout << "start line size = " << size1 << endl;
    // build headers
    if(headers == nullptr || headers->size() == 0){
        for(int i = 0; i < (int)DELIMITER.length(); i ++){
            if(len == cap){
                int new_cap = cap + 1000;
                char* new_buffer = new char[new_cap];
                memcpy(new_buffer, res, len);
                delete res;
                res = new_buffer;
                cap = new_cap;
            }
            res[len ++] = DELIMITER[i];
        }
    } else {
        for(string h : *headers){
            for(int i = 0; i < (int)h.length(); i ++){
                if(len == cap){
                    int new_cap = cap + 1000;
                    char* new_buffer = new char[new_cap];
                    memcpy(new_buffer, res, len);
                    delete res;
                    res = new_buffer;
                    cap = new_cap;
                }
                res[len ++] = h[i];
            }
        }
    }
    if(headers != nullptr){
        delete headers;
    }
    for(int i = 0; i < (int)DELIMITER.length(); i ++){
        if(len == cap){
            int new_cap = cap + 1000;
            char* new_buffer = new char[new_cap];
            memcpy(new_buffer, res, len);
            delete res;
            res = new_buffer;
            cap = new_cap;
        }
        res[len ++] = DELIMITER[i];
    }
    int size2 = len - size1;
    cout << "header line size = " << size2 << endl;
    // build body
    for(int i = 0; i < msg_len; i ++){
        if(len == cap){
            int new_cap = cap + 1000;
            char* new_buffer = new char[new_cap];
            memcpy(new_buffer, res, len);
            delete res;
            res = new_buffer;
            cap = new_cap;
        }
        res[len ++] = msg[i];
    }
    int size3 = len - size1 - size2;
    cout << "body size = " << size3 << endl;
    msg_len = len;
    return res;
}

vector<string>* _build_headers(string path){
    vector<string> *headers = new vector<string>;
    headers->push_back("Connection: close" + DELIMITER);
    char buf[100];
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    headers->push_back("Date: " + string(buf) + DELIMITER);
    if(path.find(".txt") != string::npos){
        headers->push_back("Content-Type: text/plain" + DELIMITER);
    } else if(path.find(".html") != string::npos) {
        headers->push_back("Content-Type: text/html" + DELIMITER);
    } else if(path.find(".htm") != string::npos) {
        headers->push_back("Content-Type: text/htm" + DELIMITER);
    } else if(path.find(".css") != string::npos) {
        headers->push_back("Content-Type: text/css" + DELIMITER);
    } else if(path.find(".jpg") != string::npos || path.find(".jpeg") != string::npos) {
        headers->push_back("Content-Type: image/jpeg" + DELIMITER);
    } else if(path.find(".png") != string::npos) {
        headers->push_back("Content-Type: image/png" + DELIMITER);
    } else {
        headers->push_back("Content-Type: text/plain" + DELIMITER);// default
    }
    return headers;
}

int send_msg(int sockFD, string status_code){
    stringstream ss;
    ss << "HTTP/1.1 " << status_code << DELIMITER;
    ss << "Connection: close" << DELIMITER << DELIMITER;
    string s = ss.str();
    char* msg = new char[s.length()];
    memcpy(msg, s.c_str(), s.length());
    return _send_msg(sockFD, msg, s.length());
}

/**
 * send a file with the relative path back to client
 * @param sockFD the sock fd
 * @param path the relative path of this file/directory(may not exist)
 */
int send_file(int sockFD, string path){
    // check if the thing here specified by path is a file
    if(!is_file(path)){
        if(path.size() == 0){
            path = "index.html";
        } else {
            if(path[path.size() - 1] == '/'){
                path += "index.html";
            } else {
                path += "/index.html";
            }
        }
    }
    if(path.find("..") != string::npos){
        send_msg(sockFD, BAD_REQUEST);
        return -1;
    }
    ifstream file("web_root/" + path);
    cout << "file path: " << path << endl;
    int cap = 1000, len = 0;
    char* buffer = new char[cap];
    char ch;
    while(file.get(ch)){
        buffer[len ++] = ch;
        if(len == cap){
            int new_cap = cap + 1000;
            char* new_buffer = new char[new_cap];
            memcpy(new_buffer, buffer, len);
            delete buffer;
            buffer = new_buffer;
            cap = new_cap;
        }
    }
    char* resp = _build_response(OK, _build_headers(path), buffer, len);
    char* msg = new char[len];
    memcpy(msg, resp, len);
    delete resp;
    return _send_msg(sockFD, msg, len);
}

int parse_req(char* data, int size){
    // parse start line
    int start_line_pos = find_delimiter(data, size, 0);
    if(start_line_pos == -1){
        return NOT_COMPLETE_DATA;// something wrong in the start line
    }
    char* start_line_chs = new char[start_line_pos];
    memcpy(start_line_chs, data, start_line_pos);
    string start_line = string(start_line_chs);
    delete start_line_chs;
    start_line_pos += 2;
    size_t pos = -1;
    if((pos = start_line.find(" ")) != string::npos){
        // parse method
        if(!is_get(start_line.substr(0, pos))){
            method_typ = OTHERS;// not supported method, only GET is allowed
        } else {
            method_typ = GET;
        }
        // parse path
        start_line.erase(0, pos + 1);
        if((pos=start_line.find(" ")) == string::npos){
            return NOT_COMPLETE_DATA;
        }
        req_path = start_line.substr(0, pos);
    }
    // parse header line
    int head_line_pos;
    while((head_line_pos = find_delimiter(data, size, start_line_pos)) != -1){
        if(head_line_pos == -1){
            return NOT_COMPLETE_DATA;// something wrong in head lines
        }
        if(head_line_pos == start_line_pos){
            start_line_pos += 2;
            break;
        }
        char* header_line = new char[head_line_pos - start_line_pos + 1];
        memcpy(header_line, data + start_line_pos, head_line_pos - start_line_pos);
        header_line[head_line_pos - start_line_pos] = '\0';
        cout << header_line << endl;
        delete header_line;
        start_line_pos = head_line_pos + 2;
    }
    // parse body if it exists
    if(size - start_line_pos > 0){
        char* body = new char[size - start_line_pos + 1];
        memcpy(body, data + start_line_pos, size - start_line_pos);
        body[size - start_line_pos] = '\0';
        cout << body << endl;
        delete body;
    }
    return PARSE_REQ_SUCCESS;
}


void process(int cli_sockFD){
    char *buffer = new char[BUFFERSIZE];
    int req_cap = 1000, req_len = 0;
    char *req = new char[req_cap];
    // init variables for this request
    method_typ = UNKNOWN;
    req_path = ""; 
    // read request information(Given the format of the request is correct)
    while(true){
        int size = recv(cli_sockFD, buffer, BUFFERSIZE, 0);
        if(size == -1){
            cerr << "recv data failed" << gai_strerror(size) << endl;
            delete req;
            delete buffer;
            send_msg(cli_sockFD, INTERNAL_ERROR);
            return ;
        }
        if(size == 0){
            break;
        }
        if(size > (req_cap - req_len)){
            int new_req_cap = req_cap + 1000;
            char* new_req = new char[new_req_cap];
            memset(new_req, 0, new_req_cap);
            memcpy(new_req, req, req_len);
            delete req;
            req = new_req;
            req_cap = new_req_cap;
        }
        memcpy(&req[req_len], buffer, size);
        req_len += size;
        if(parse_req(req, req_len) == PARSE_REQ_SUCCESS){
            break;
        }
    }
    delete req;
    delete buffer;
    if(method_typ == GET){
        send_file(cli_sockFD, req_path);
    } else {
        send_msg(cli_sockFD, NOT_IMPLEMENTED);
    }
}


int main(int argc, char *argv[]){
    int port, sockFD, error;
    char* temp;
    struct sockaddr_in serv_addr;

    if(argc != 2){
        cerr << "Usage ./http_svr port_number" << endl;
        return EXIT_FAILURE;
    }

    port = strtol(argv[1], &temp, 10);
    if(*temp != '\0'){
        cerr << "port number isn't base of 10" << endl;
        delete temp;
        return EXIT_FAILURE;
    }

    if((sockFD = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr << "fail to create socket, " << gai_strerror(sockFD) << endl;
        return EXIT_FAILURE;
    }

    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    
    error = bind(sockFD, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if(error < 0){
        close(sockFD);
        cout << "fail to bind, " << gai_strerror(error) << endl;
        return EXIT_FAILURE;
    }

    error = listen(sockFD, MAX_CONNS);
    if(error < 0){
        close(sockFD);
        cerr << "fail to listen the port: " << port << gai_strerror(error) << endl;
        return EXIT_FAILURE;
    }

    // keep listening
    while(true){
        struct sockaddr_in cli_addr;
        unsigned int sizelen = sizeof(cli_addr);
        int cli_sockFD = accept(sockFD, (struct sockaddr*)&cli_addr, &sizelen);
        if(cli_sockFD < 0){
            cerr << "fail to accept the client, " << gai_strerror(error) << endl;
            continue;
        }
        process(cli_sockFD);
        close(cli_sockFD);
    }
}