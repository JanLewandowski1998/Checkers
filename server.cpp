#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdexcept>
#include <mutex> 

#include "message.h"
#include "game.h"

#define buf_size 128
#define games_size 1024
#define players_size 1024

//global variables:
Game* games = new Game[games_size];
int gamesCount = 0;
Player* players = new Player[players_size];
int playersCount = 0;

//mutexes
std::mutex gamesMutex;
std::mutex playersMutex;

//connection descriptor
//holding clients socket and address
struct Descriptor {
    int socket;
    struct sockaddr_in address;
};

//receiving and handling messages from client
void *serve_single_client(void *arg) 
{
    //create client descriptor
    struct Descriptor* clientDescriptor = (struct Descriptor*)arg;

    printf("new connection from: %s:%d\n", 
            inet_ntoa(clientDescriptor->address.sin_addr), 
            clientDescriptor->address.sin_port);

    while(1) 
    {
        //recive message string
        char buf[buf_size] = "";
        int bytesRead = read(clientDescriptor->socket, buf, buf_size);
        if (bytesRead < 0)
        {
            perror("read() ERROR");
            exit(1);
        }
        else if (bytesRead == 0)
        {
            continue;
        }

        //create message object
        Message recivedMessage(clientDescriptor->address.sin_port, buf);
        printf("New message from: %d tag: %d message: %s\n", 
                recivedMessage.userID, recivedMessage.tag, recivedMessage.message.c_str());

        switch (recivedMessage.tag)
        {
        case TAG_JOIN_RANDOM_GAME:
            //TODO
            break;

        case TAG_JOIN_GAME:
        {
            //recivedMessage: <ip>;<nick>

            int gameIP = stoi(recivedMessage.message.substr(0, 
                    recivedMessage.message.find(";")));
            recivedMessage.message.erase(0, recivedMessage.message.find(";") + 1);

            std::string nick = recivedMessage.message;

            playersMutex.lock();

            //find our player
            Player* ourPlayer = getPlayer(players, 
                    playersCount, recivedMessage.userID);

            //hosting player isn't stored in players array
            if(ourPlayer == nullptr)
            {
                //create player and add to players array
                Player player(recivedMessage.userID, nick, clientDescriptor->socket);

                players[playersCount] = player;
                playersCount++;

                ourPlayer = &players[playersCount - 1];
            }

            gamesMutex.lock();

            //wrong game ip
            if(gamesCount <= gameIP)
            {
                //create and send response to our player
                Message response(0, TAG_WRONG_IP, "");
                response.sendto(ourPlayer->clientSocket);

                break;
            }

            //find game by ip
            Game* game = &games[gameIP]; 

            //assign our player to the game
            ourPlayer->gameID = gameIP;
            
            Player* opponent;
            Color ourPlayerColor;
            Color opponentColor;

            //our player will play black
            if(game->blackPlayer == nullptr)
            {
                game->blackPlayer = ourPlayer;
                ourPlayerColor = BLACK;

                opponent = game->whitePlayer;
                opponentColor = WHITE;
            }
            //our player will play white
            if(game->whitePlayer == nullptr)
            {
                game->whitePlayer = ourPlayer;
                ourPlayerColor = WHITE;

                opponent = game->blackPlayer;
                opponentColor = BLACK;
            }

            //start game
            game->status = ONGOING;

            gamesMutex.unlock();

            //create and send response to our player
            std::string ourPlayerMessage = opponent->nick + 
                    ";" + std::to_string(ourPlayerColor);
            Message ourPlayerResponse(0, TAG_GAME_STARTED, ourPlayerMessage);
            ourPlayerResponse.sendto(ourPlayer->clientSocket);

            //create and send response to opponent
            std::string opponentMessage = (std::string)(ourPlayer->nick + 
                    ";" + std::to_string(opponentColor));
            Message opponentResponse(0, TAG_GAME_STARTED, opponentMessage);
            opponentResponse.sendto(opponent->clientSocket);

            playersMutex.unlock();

            break;
        }

        case TAG_HOST_GAME:
        {
            //recivedMessage: <nick>
            std::string nick = recivedMessage.message;

            playersMutex.lock();

            Player* hostPlayer = getPlayer(players, 
            playersCount, recivedMessage.userID);

            //hosting player isn't stored in players array
            if(hostPlayer == nullptr)
            {
                //create player and add to players array
                Player player(recivedMessage.userID, nick, clientDescriptor->socket);
                players[playersCount] = player;
                playersCount++;

                hostPlayer = &players[playersCount - 1];
            }

            gamesMutex.lock();

            //create game and add to games array
            Game game(gamesCount, hostPlayer);
            games[gamesCount] = game;
            gamesCount++;
            
            gamesMutex.unlock();

            hostPlayer->gameID = game.id;

            //create and send response to our host player
            Message response(0, TAG_GAME_HOSTED, std::to_string(game.id));
            response.sendto(hostPlayer->clientSocket);

            playersMutex.unlock();

            break;
        }

        case TAG_PAWN_MOVED:
        {
            //recivedMessage: 
            //<beginning_row>;<beginning_col>;<finish_row>;<finish_col>

            //find the opponent
            playersMutex.lock();
            gamesMutex.lock();
            Player* opponent = getOpponent(players, playersCount, games, 
                    gamesCount, recivedMessage.userID);
            gamesMutex.unlock();

            //no opponent found
            if(opponent == nullptr)
            {
                throw std::invalid_argument("Player with this id doesn't exist or have no opponent");
            }
            
            //create and send response to opponent
            Message response(0, TAG_PAWN_MOVED, recivedMessage.message);
            response.sendto(opponent->clientSocket);

            playersMutex.unlock();

            break;
        }

        case TAG_SURRENDER:
            //TODO
            break;

        case TAG_OFFER_DRAW:
            //TODO
            break;

        case TAG_ACCEPT_DRAW:
            //TODO
            break;
        
        default:
            break;
        }
    }
}

int main() 
{
    //create address for communication purposes
    struct sockaddr_in addr;
    addr.sin_family = PF_INET;
    addr.sin_port = htons(7777);
    addr.sin_addr.s_addr = INADDR_ANY;

    //create socket
    //all messages are transmited using sockets
    int sock = socket(PF_INET, SOCK_STREAM, 0);

    //set the port on which we'll be listening
    bind(sock, (struct sockaddr*) &addr, sizeof(addr));

    //create listening socket
    //10 connections max
    listen(sock, 10);
    
    while(1) 
    {
        //create client descriptor
        struct Descriptor* clientDescriptor = new Descriptor;

        //accept connection from client
        socklen_t addrLen = sizeof(clientDescriptor->address);
        clientDescriptor->socket = accept(sock, 
                (struct sockaddr*) &clientDescriptor->address,
                &addrLen);

        //thread receiving and handling messages from client
        pthread_t tid;
        pthread_create(&tid, NULL, serve_single_client, clientDescriptor);
        
        //example how to send a message to the client
        /*
        int reciverSocket = clientDescriptor->socket;
        int t = TAG_JOIN_RANDOM_GAME;
        std::string str = "a1";
        Message mes(0, t, str);

        for(int i = 0; i < 3; i++)
        {
            mes.sendto(reciverSocket);
            sleep(0.2);
        }
        */

        pthread_detach(tid);
    }

    close(sock);
}