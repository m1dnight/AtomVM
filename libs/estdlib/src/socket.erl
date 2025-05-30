%
% This file is part of AtomVM.
%
% Copyright 2023 Fred Dushin <fred@dushin.net>
%
% Licensed under the Apache License, Version 2.0 (the "License");
% you may not use this file except in compliance with the License.
% You may obtain a copy of the License at
%
%    http://www.apache.org/licenses/LICENSE-2.0
%
% Unless required by applicable law or agreed to in writing, software
% distributed under the License is distributed on an "AS IS" BASIS,
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
% See the License for the specific language governing permissions and
% limitations under the License.
%
% SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
%

-module(socket).

-export([
    open/3,
    close/1,
    bind/2,
    listen/1, listen/2,
    accept/1,
    accept/2,
    sockname/1,
    peername/1,
    recv/1,
    recv/2,
    recv/3,
    recvfrom/1,
    recvfrom/2,
    recvfrom/3,
    send/2,
    sendto/3,
    setopt/3,
    getopt/2,
    connect/2,
    shutdown/2
]).

%% internal nifs
-export([
    nif_select_read/2,
    nif_accept/1,
    nif_recv/2,
    nif_recvfrom/2,
    nif_select_stop/1,
    nif_send/2,
    nif_sendto/3
]).

-opaque socket() :: {reference(), any()}.
-type domain() :: inet.
-type type() :: stream | dgram.
-type protocol() :: tcp | udp.
-type sockaddr() :: sockaddr_in().
-type sockaddr_in() :: #{
    family := inet,
    port := port_number(),
    addr := any | loopback | in_addr()
}.
-type in_addr() :: {0..255, 0..255, 0..255, 0..255}.
-type port_number() :: 0..65535.
-type ip_mreq() :: #{multiaddr := in_addr(), interface := in_addr()}.

-type socket_option() ::
    {socket, reuseaddr | linger | type}
    | {otp, recvbuf}
    | {ip, add_membership}.

-export_type([
    socket/0,
    domain/0,
    type/0,
    protocol/0,
    sockaddr/0,
    sockaddr_in/0,
    in_addr/0,
    port_number/0,
    socket_option/0,
    ip_mreq/0
]).

-define(DEFAULT_BACKLOG, 4).

%% -define(TRACE(Fmt, Args), io:format(Fmt, Args)).
-define(TRACE(Fmt, Args), ok).

%%-----------------------------------------------------------------------------
%% @param   Domain the network domain
%% @param   Type the network type
%% @param   Protocol the network protocol
%% @returns `{ok, Socket}' if successful; `{error, Reason}', otherwise.
%% @doc     Create a socket.
%%
%%          Create a socket with a specified domain, type, and protocol.
%%          Use the returned socket for communications.
%%
%% Example:
%%
%%          `{ok, ListeningSocket} = socket:open(inet, stream, tcp)'
%% @end
%%-----------------------------------------------------------------------------
-spec open(Domain :: domain(), Type :: type(), Protocol :: protocol()) ->
    {ok, socket()} | {error, Reason :: term()}.
open(_Domain, _Type, _Protocol) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Close a socket.
%%
%%          Close a previously opened socket.
%%
%% Example:
%%
%%          `ok = socket:close(Socket)'
%% @end
%%-----------------------------------------------------------------------------
-spec close(Socket :: socket()) -> ok | {error, Reason :: term()}.
close(_Socket) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Address the address to which to bind the socket
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Bind a socket to an interface.
%%
%%          Bind a socket to an interface, via a socket address.  Use `any' to
%%          bind to all interfaces.  Use `loopback' to bind to the loopback
%%          address.  To specify a port, use a map containing the network family,
%%          address, and port.
%%
%% Example:
%%
%%          `ok = socket:bind(ListeningSocket, #{family => inet, addr => any, port => 44404})'
%% @end
%%-----------------------------------------------------------------------------
-spec bind(Socket :: socket(), Address :: sockaddr() | any | loopback) ->
    ok | {error, Reason :: term()}.
bind(_Socket, _Address) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Set the socket to listen for connections.
%%
%%          Listen for connections.  The socket should be a connection-based
%%          socket and should be bound to an address and port.
%%
%% Example:
%%
%%          `ok = socket:listen(ListeningSocket)'
%% @end
%%-----------------------------------------------------------------------------
-spec listen(Socket :: socket()) -> ok | {error, Reason :: term()}.
listen(Socket) ->
    ?MODULE:listen(Socket, ?DEFAULT_BACKLOG).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Backlog the maximum length for the queue of pending connections
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Set the socket to listen for connections.
%%
%%          Listen for connections.  The socket should be a connection-based
%%          socket and should be bound to an address and port.
%%
%%          Use the Backlog to specify the maximum length for the queue of
%%          pending connections
%%
%% Example:
%%
%%          `ok = socket:listen(ListeningSocket, 4)'
%% @end
%%-----------------------------------------------------------------------------
-spec listen(Socket :: socket(), Backlog :: integer()) -> ok | {error, Reason :: term()}.
listen(_Socket, _Backlog) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @returns `{ok, Address}' if successful; `{error, Reason}', otherwise.
%% @doc     Return the current address for the specified socket.
%%
%% Example:
%%
%%          `{ok, Address} = socket:sockname(ConnectedSocket)'
%% @end
%%-----------------------------------------------------------------------------
-spec sockname(Socket :: socket()) -> {ok, Address :: sockaddr()} | {error, Reason :: term()}.
sockname(_Socket) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @returns `{ok, Address}' if successful; `{error, Reason}', otherwise.
%% @doc     Return the address of the peer connected to the specified socket.
%%
%% Example:
%%
%%          `{ok, Address} = socket:peername(ConnectedSocket)'
%% @end
%%-----------------------------------------------------------------------------
-spec peername(Socket :: socket()) -> {ok, Address :: sockaddr()} | {error, Reason :: term()}.
peername(_Socket) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @equiv socket:accept(ListeningSocket, infinity)
%% @end
%%-----------------------------------------------------------------------------
-spec accept(Socket :: socket()) -> {ok, Connection :: socket()} | {error, Reason :: term()}.
accept(Socket) ->
    accept(Socket, infinity).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Timeout timeout (in milliseconds)
%% @returns `{ok, Connection}' if successful; `{error, Reason}', otherwise.
%% @doc     Wait for the socket to accept a connection.
%%
%%          Wait for the socket to accept a connection.  The socket should
%%          be set to listen for connections.
%%
%%          Note that this function will block until a connection is made
%%          from a client, unless `nowait' or a reference is passed as `Timeout'.
%%          Typically, users will spawn a call to `accept' in a separate process.
%%
%% Example:
%%
%%          `{ok, ConnectedSocket} = socket:accept(ListeningSocket)'
%% @end
%%-----------------------------------------------------------------------------
-spec accept(Socket :: socket(), Timeout :: timeout() | nowait | reference()) ->
    {ok, Connection :: socket()}
    | {select, {select_info, accept, reference()}}
    | {error, Reason :: term()}.
accept(Socket, 0) ->
    accept0_noselect(Socket);
accept(Socket, nowait) ->
    accept0_nowait(Socket, erlang:make_ref());
accept(Socket, Ref) when is_reference(Ref) ->
    accept0_nowait(Socket, Ref);
accept(Socket, Timeout) ->
    accept0(Socket, Timeout).

accept0_noselect(Socket) ->
    case ?MODULE:nif_accept(Socket) of
        {error, _} = E ->
            E;
        {ok, _Socket} = Reply ->
            Reply
    end.

accept0(Socket, Timeout) ->
    Ref = erlang:make_ref(),
    case ?MODULE:nif_select_read(Socket, Ref) of
        ok ->
            receive
                {'$socket', Socket, select, Ref} ->
                    case ?MODULE:nif_accept(Socket) of
                        {error, _} = E ->
                            ?MODULE:nif_select_stop(Socket),
                            E;
                        {ok, _Socket} = Reply ->
                            Reply
                    end;
                {'$socket', Socket, abort, {Ref, closed}} ->
                    % socket was closed by another process
                    % TODO: we need to handle:
                    % (a) SELECT_STOP being scheduled
                    % (b) flush of messages as we can have both in the
                    % queue
                    {error, closed}
            after Timeout ->
                {error, timeout}
            end;
        {error, _Reason} = Error ->
            Error
    end.

accept0_nowait(Socket, Ref) ->
    case ?MODULE:nif_accept(Socket) of
        {error, eagain} ->
            case ?MODULE:nif_select_read(Socket, Ref) of
                ok ->
                    {select, {select_info, accept, Ref}};
                {error, _} = SelectError ->
                    SelectError
            end;
        {error, _} = RecvError ->
            RecvError;
        {ok, _Socket} = Reply ->
            Reply
    end.

%%-----------------------------------------------------------------------------
%% @equiv socket:recv(Socket, 0)
%% @end
%%-----------------------------------------------------------------------------
-spec recv(Socket :: socket()) -> {ok, Data :: binary()} | {error, Reason :: term()}.
recv(Socket) ->
    recv(Socket, 0, infinity).

%%-----------------------------------------------------------------------------
%% @equiv socket:recv(Socket, Length, infinity)
%% @end
%%-----------------------------------------------------------------------------
-spec recv(Socket :: socket(), Length :: non_neg_integer()) ->
    {ok, Data :: binary()} | {error, Reason :: term()}.
recv(Socket, Length) ->
    recv(Socket, Length, infinity).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Length number of bytes to receive
%% @param   Timeout timeout (in milliseconds)
%% @returns `{ok, Data}' if successful; `{error, Reason}', otherwise.
%% @doc     Receive data on the specified socket.
%%
%% This function is equivalent to `recvfrom/3' except for the return type.
%%
%% Example:
%%
%%          `{ok, Data} = socket:recv(ConnectedSocket)'
%% @end
%%-----------------------------------------------------------------------------
-spec recv(
    Socket :: socket(), Length :: non_neg_integer(), Timeout :: timeout() | nowait | reference()
) ->
    {ok, Data :: binary()}
    | {select, {select_info, recvfrom, reference()}}
    | {select, {{select_info, recvfrom, reference()}, Data :: binary()}}
    | {error, Reason :: term()}.
recv(Socket, Length, 0) ->
    recv0_noselect(Socket, Length);
recv(Socket, 0, Timeout) when is_integer(Timeout) orelse Timeout =:= infinity ->
    recv0(Socket, 0, Timeout);
recv(Socket, Length, nowait) ->
    recv0_nowait(Socket, Length, erlang:make_ref());
recv(Socket, Length, Ref) when is_reference(Ref) ->
    recv0_nowait(Socket, Length, Ref);
recv(Socket, Length, Timeout) ->
    case ?MODULE:getopt(Socket, {socket, type}) of
        {ok, stream} when Timeout =/= infinity ->
            recv0_r(Socket, Length, Timeout, erlang:system_time(millisecond) + Timeout, []);
        {ok, stream} when Timeout =:= infinity ->
            recv0_r(Socket, Length, Timeout, undefined, []);
        _ ->
            recv0(Socket, Length, Timeout)
    end.

recv0_noselect(Socket, Length) ->
    case ?MODULE:nif_recv(Socket, Length) of
        {error, _} = E ->
            E;
        {ok, Data} when Length =:= 0 orelse byte_size(Data) =:= Length ->
            {ok, Data};
        {ok, Data} ->
            case ?MODULE:getopt(Socket, {socket, type}) of
                {ok, stream} ->
                    {error, {timeout, Data}};
                {ok, dgram} ->
                    {ok, Data}
            end
    end.

recv0(Socket, Length, Timeout) ->
    Ref = erlang:make_ref(),
    case ?MODULE:nif_select_read(Socket, Ref) of
        ok ->
            receive
                {'$socket', Socket, select, Ref} ->
                    case ?MODULE:nif_recv(Socket, Length) of
                        {error, _} = E ->
                            ?MODULE:nif_select_stop(Socket),
                            E;
                        {ok, Data} ->
                            {ok, Data}
                    end;
                {'$socket', Socket, abort, {Ref, closed}} ->
                    % socket was closed by another process
                    % TODO: see above in accept/2
                    {error, closed}
            after Timeout ->
                {error, timeout}
            end;
        {error, _Reason} = Error ->
            Error
    end.

recv0_nowait(Socket, Length, Ref) ->
    case ?MODULE:nif_recv(Socket, Length) of
        {error, Reason} when Reason =:= timeout orelse Reason =:= eagain ->
            case ?MODULE:nif_select_read(Socket, Ref) of
                ok ->
                    {select, {select_info, recv, Ref}};
                {error, _} = Error1 ->
                    Error1
            end;
        {error, _} = E ->
            E;
        {ok, Data} when byte_size(Data) < Length ->
            case ?MODULE:getopt(Socket, {socket, type}) of
                {ok, stream} ->
                    case ?MODULE:nif_select_read(Socket, Ref) of
                        ok ->
                            {select, {{select_info, recv, Ref}, Data}};
                        {error, _} = Error1 ->
                            Error1
                    end;
                {ok, dgram} ->
                    {ok, Data}
            end;
        {ok, Data} ->
            {ok, Data}
    end.

recv0_r(Socket, Length, Timeout, EndQuery, Acc) ->
    Ref = erlang:make_ref(),
    case ?MODULE:nif_select_read(Socket, Ref) of
        ok ->
            receive
                {'$socket', Socket, select, Ref} ->
                    case ?MODULE:nif_recv(Socket, Length) of
                        {error, _} = E ->
                            ?MODULE:nif_select_stop(Socket),
                            E;
                        {ok, Data} ->
                            NewAcc = [Data | Acc],
                            Remaining = Length - byte_size(Data),
                            case Remaining of
                                0 ->
                                    {ok, list_to_binary(lists:reverse(NewAcc))};
                                _ ->
                                    NewTimeout =
                                        case Timeout of
                                            infinity -> infinity;
                                            _ -> EndQuery - erlang:system_time(millisecond)
                                        end,
                                    recv0_r(Socket, Remaining, NewTimeout, EndQuery, NewAcc)
                            end
                    end;
                {'$socket', Socket, abort, {Ref, closed}} ->
                    % socket was closed by another process
                    % TODO: see above in accept/2
                    {error, closed}
            after Timeout ->
                case Acc of
                    [] -> {error, timeout};
                    _ -> {error, {timeout, list_to_binary(lists:reverse(Acc))}}
                end
            end;
        {error, _Reason} = Error ->
            Error
    end.

%%-----------------------------------------------------------------------------
%% @equiv socket:recvfrom(Socket, 0)
%% @end
%%-----------------------------------------------------------------------------
-spec recvfrom(Socket :: socket()) ->
    {ok, {Address :: sockaddr(), Data :: binary()}} | {error, Reason :: term()}.
recvfrom(Socket) ->
    recvfrom(Socket, 0).

%%-----------------------------------------------------------------------------
%% @equiv socket:recvfrom(Socket, Length, infinity)
%% @end
%%-----------------------------------------------------------------------------
-spec recvfrom(Socket :: socket(), Length :: non_neg_integer()) ->
    {ok, {Address :: sockaddr(), Data :: binary()}} | {error, Reason :: term()}.
recvfrom(Socket, Length) ->
    recvfrom(Socket, Length, infinity).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Length number of bytes to receive
%% @param   Timeout timeout (in milliseconds)
%% @returns `{ok, {Address, Data}}' if successful; `{error, Reason}', otherwise.
%% @doc     Receive data on the specified socket, returning the from address.
%%
%%          Note that this function will block until data is received
%%          on the socket.
%%
%% Example:
%%
%%          `{ok, {Address, Data}} = socket:recvfrom(ConnectedSocket)'
%%
%% If socket is UDP, the function retrieves the first available packet and
%% truncate it to Length bytes, unless Length is 0 in which case it returns
%% the whole packet ("all available").
%%
%% If socket is TCP and Length is 0, this function retrieves all available
%% data without waiting (using peek if the platform allows it).
%% If socket is TCP and Length is not 0, this function waits until Length
%% bytes are available and return these bytes.
%% @end
%%-----------------------------------------------------------------------------
-spec recvfrom(
    Socket :: socket(), Length :: non_neg_integer(), Timeout :: timeout() | nowait | reference()
) ->
    {ok, {Address :: sockaddr(), Data :: binary()}}
    | {select, {select_info, recvfrom, reference()}}
    | {error, Reason :: term()}.
recvfrom(Socket, Length, 0) ->
    recvfrom0_noselect(Socket, Length);
recvfrom(Socket, Length, nowait) ->
    recvfrom0_nowait(Socket, Length, erlang:make_ref());
recvfrom(Socket, Length, Ref) when is_reference(Ref) ->
    recvfrom0_nowait(Socket, Length, Ref);
recvfrom(Socket, Length, Timeout) ->
    recvfrom0(Socket, Length, Timeout).

recvfrom0_noselect(Socket, Length) ->
    case ?MODULE:nif_recvfrom(Socket, Length) of
        {error, _} = E ->
            E;
        {ok, {_Address, _Data}} = Reply ->
            Reply
    end.

recvfrom0(Socket, Length, Timeout) ->
    Ref = erlang:make_ref(),
    case ?MODULE:nif_select_read(Socket, Ref) of
        ok ->
            receive
                {'$socket', Socket, select, Ref} ->
                    case ?MODULE:nif_recvfrom(Socket, Length) of
                        {error, _} = E ->
                            ?MODULE:nif_select_stop(Socket),
                            E;
                        {ok, {_Address, _Data}} = Reply ->
                            Reply
                    end;
                {'$socket', Socket, abort, {Ref, closed}} ->
                    % socket was closed by another process
                    % TODO: see above in accept/2
                    {error, closed}
            after Timeout ->
                {error, timeout}
            end;
        {error, _Reason} = Error ->
            Error
    end.

recvfrom0_nowait(Socket, Length, Ref) ->
    case ?MODULE:nif_recvfrom(Socket, Length) of
        {error, timeout} ->
            case ?MODULE:nif_select_read(Socket, Ref) of
                ok ->
                    {select, {select_info, recvfrom, Ref}};
                {error, _} = SelectError ->
                    SelectError
            end;
        {error, _} = RecvError ->
            RecvError;
        {ok, {_Address, _Data}} = Reply ->
            Reply
    end.

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Data the data to send
%% @returns `{ok, Rest}' if successful; `{error, Reason}', otherwise.
%% @doc     Send data on the specified socket.
%%
%%          Note that this function will block until data is sent
%%          on the socket.  The data may not have been received by the
%%          intended recipient, and the data may not even have been sent
%%          over the network.
%%
%% Example:
%%
%%          `ok = socket:send(ConnectedSocket, Data)'
%% @end
%%-----------------------------------------------------------------------------
-spec send(Socket :: socket(), Data :: iodata()) ->
    ok | {ok, Rest :: binary()} | {error, Reason :: term()}.
send(Socket, Data) when is_binary(Data) ->
    ?MODULE:nif_send(Socket, Data);
send(Socket, Data) ->
    ?MODULE:nif_send(Socket, erlang:iolist_to_binary(Data)).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Data the data to send
%% @param   Dest the destination to which to send the data
%% @returns `{ok, Rest}' if successful; `{error, Reason}', otherwise.
%% @doc     Send data on the specified socket to the specified destination.
%%
%%          Note that this function will block until data is sent
%%          on the socket.  The data may not have been received by the
%%          intended recipient, and the data may not even have been sent
%%          over the network.
%%
%% Example:
%%
%%          `ok = socket:sendto(ConnectedSocket, Data, Dest)'
%% @end
%%-----------------------------------------------------------------------------
-spec sendto(Socket :: socket(), Data :: iodata(), Dest :: sockaddr()) ->
    ok | {ok, Rest :: binary()} | {error, Reason :: term()}.
sendto(Socket, Data, Dest) when is_binary(Data) ->
    ?MODULE:nif_sendto(Socket, Data, Dest);
sendto(Socket, Data, Dest) ->
    ?MODULE:nif_sendto(Socket, erlang:iolist_to_binary(Data), Dest).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   SocketOption the option
%% @returns `{ok, Value}' if successful; `{error, Reason}', otherwise.
%% @doc     Get a socket option.
%%
%%          Currently, the following options are supported:
%%          <table>
%%              <tr><td>`{socket, type}'</td><td>`type()'</td></tr>
%%          </table>
%%
%% Example:
%%
%%      `{ok, stream} = socket:getopt(ListeningSocket, {socket, type})'
%% @end
%%-----------------------------------------------------------------------------
-spec getopt(Socket :: socket(), SocketOption :: socket_option()) ->
    {ok, Value :: term()} | {error, Reason :: term()}.
getopt(_Socket, _SocketOption) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   SocketOption the option
%% @param   Value the option value
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Set a socket option.
%%
%%          Set an option on a socket.
%%
%%          Currently, the following options are supported:
%%          <table>
%%              <tr><td>`{socket, reuseaddr}'</td><td>`boolean()'</td></tr>
%%              <tr><td>`{socket, linger}'</td><td>`#{onoff => boolean(), linger => non_neg_integer()}'</td></tr>
%%              <tr><td>`{otp, recvbuf}'</td><td>`non_neg_integer()'</td></tr>
%%              <tr><td>`{ip, add_membership}'</td><td>`ip_mreq()'</td></tr>
%%          </table>
%%
%% Example:
%%
%%      `ok = socket:setopt(ListeningSocket, {socket, reuseaddr}, true)'
%%      `ok = socket:setopt(ListeningSocket, {socket, linger}, #{onoff => true, linger => 0})'
%% @end
%%-----------------------------------------------------------------------------
-spec setopt(Socket :: socket(), SocketOption :: socket_option(), Value :: term()) ->
    ok | {error, any()}.
setopt(_Socket, _SocketOption, _Value) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   Address the address to which to connect the socket
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Wait for the socket to connect to an address.
%%
%%          Wait for the socket to connect to an address.  The socket should
%%          be a connection-based socket.
%%
%%          Note that this function will block until a connection is made
%%          to a server.
%%
%% Example:
%%
%%          `ok = socket:connect(Socket, #{family => inet, addr => loopback, port => 44404})'
%% @end
%%-----------------------------------------------------------------------------
-spec connect(Socket :: socket(), Address :: sockaddr()) -> ok | {error, Reason :: term()}.
connect(_Socket, _Address) ->
    erlang:nif_error(undefined).

%%-----------------------------------------------------------------------------
%% @param   Socket the socket
%% @param   How how to shut the socket down
%% @returns `ok' if successful; `{error, Reason}', otherwise.
%% @doc     Shut down one or both ends of a full-duplex socket connection.
%%
%% Example:
%%
%%          `ok = socket:shutdown(Socket, read_write)'
%% @end
%%-----------------------------------------------------------------------------
-spec shutdown(Socket :: socket(), How :: read | write | read_write) ->
    ok | {error, Reason :: term()}.
shutdown(_Socket, _How) ->
    erlang:nif_error(undefined).
%%
%% Internal functions
%%

%% @private
nif_select_read(_Socket, _Ref) ->
    erlang:nif_error(undefined).

%% @private
nif_accept(_Socket) ->
    erlang:nif_error(undefined).

%% @private
nif_recv(_Socket, _Length) ->
    erlang:nif_error(undefined).

%% @private
nif_recvfrom(_Socket, _Length) ->
    erlang:nif_error(undefined).

%% @private
nif_select_stop(_Socket) ->
    erlang:nif_error(undefined).

%% @private
nif_send(_Socket, _Data) ->
    erlang:nif_error(undefined).

%% @private
nif_sendto(_Socket, _Data, _Dest) ->
    erlang:nif_error(undefined).
