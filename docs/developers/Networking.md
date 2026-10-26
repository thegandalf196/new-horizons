# Networking

## The big picture

For implementation details see files located at `lib/network` directory.

Networked VCMI sessions use TCP. New Horizons single-player instead uses an
in-process runner and internal connection, retaining serialized requests and
authoritative validation. On TCP connections, each atomic message is a serialized
byte stream preceded by a four-byte size:

```cpp
int32_t messageSize;
byte messagePayload[messageSize];
```

Networking can be used by:

- game client (vcmiclient / VCMI_Client.exe). Actual application that player interacts with directly using UI.
- match server (vcmiserver / VCMI_Server.exe / part of game client). This app controls game logic and coordinates multiplayer games.
- lobby server (vcmilobby). This app provides access to global lobby through which players can play game over Internet.

Following connections can be established during game lifetime:

- game client -> match server: This is main connection for use during gameplay, created once player requests to move from main menu to pregame / match lobby (e.g. after pressing New Game / Load Game)
- game client -> lobby server: This connection is used to access global lobby, for multiplayer over Internet. Created when player logs into a lobby (Multiplayer -> Connect to global service)
- match server -> lobby server: This connection is established when player creates new multiplayer room via lobby. It is used by lobby server to send commands to match server

## Gameplay communication

For gameplay, VCMI serializes data into a binary stream. See [Serialization](Serialization.md) for more information.

### Headless client presentation

`CClient::handlePack` applies shared game state before dispatching its
post-application client visitor. Headless startup does not create `GameEngine`,
so presentation-only visitors must return before accessing `ENGINE` or windows.
Mage Guild visits, spell research and Adventure Spell unlocks still apply their
authoritative receipts; only their window refresh is skipped. Keep state receipt
handling independent of UI availability. The maintained
`tools/tests/test_new_horizons_adventure_unlock_headless.py` check protects this
guard ordering for all three Guild refresh visitors.

## Local startup lifecycle

Start discovery only for explicit listening, after successful network startup.
Runner startup completion means preparation is ready, not that the client
connection callback has run. Preparation failure must join the failed worker,
discard partial server state and preserve the original exception; do not connect
or automatically fall back to a process/TCP retry. Serialize lifecycle calls on
the owning client thread: repeated sequential waits are not concurrent-join safety.

Cancellation must arrange cleanup even after connection establishment. Gate a new
setup/restart or retain stable session and runner ownership so old callbacks cannot
attach an old connection, reset a newer runner or race a join. Stop the event loop
thread-safely; finalize simulation state/discovery on its owner thread. Release the
interface mutex around joins while retaining the joined runner's lifetime.
Cleanup cannot depend on a future callback that may never arrive. Stopping an
internal loop is not a client disconnect notification; preserve ordinary
packet-based disconnect.

Preserve connection identity through queued packet delivery and reject packets
from a replaced connection before applying them to the current session. Client
connection allocation/posting can fail after runner readiness; join and discard
that runner before propagating the failure. Cancellation must finish connection,
retry-timer and pending-tutorial cleanup before allowing another setup. Late retry
or connection callbacks must tolerate the old session already being cancelled.

## Global lobby communication

For implementation details see:

- game client: `client/globalLobby/GlobalLobbyClient.h
- match server: `server/GlobalLobbyProcessor.h
- lobby server: `client/globalLobby/GlobalLobbyClient.h

In case of global lobby, message payload uses plaintext json format - utf-8 encoded string:

```cpp
int32_t messageSize;
char jsonString[messageSize];
```

Every message must be a struct (json object) that contains "type" field. Unlike rest of VCMI codebase, this message is validated as strict json, without any extensions, such as comments.

### Communication flow

Notes:

- invalid message, such as corrupted json format or failure to validate message will result in no reply from server
- in addition to specified messages, match server will send `operationFailed` message on failure to apply player request

#### New Account Creation

- client -> lobby: `clientRegister`
- lobby -> client: `accountCreated`

#### Login

- client -> lobby: `clientLogin`
- lobby -> client: `loginSuccess`
- lobby -> client: `chatHistory`
- lobby -> client: `activeAccounts`
- lobby -> client: `activeGameRooms`

#### Chat Message

- client -> lobby: `sendChatMessage`
- lobby -> every client: `chatMessage`

#### New Game Room

- client starts match server instance
- match -> lobby: `serverLogin`
- lobby -> match: `loginSuccess`
- match accepts connection from client
- client -> lobby: `activateGameRoom`
- lobby -> client: `joinRoomSuccess`
- lobby -> every client: `activeAccounts`
- lobby -> every client: `activeGameRooms`

#### Joining a game room

See [#Proxy mode](#proxy-mode)

#### Leaving a game room

- client closes connection to match server
- match -> lobby: `leaveGameRoom`

#### Sending an invite

- client -> lobby: `sendInvite`
- lobby -> target client: `inviteReceived`

Note: there is no dedicated procedure to accept an invite. Instead, invited player will use same flow as when joining public game room

#### Logout

- client closes connection
- lobby -> every client: `activeAccounts`

### Proxy mode

In order to connect players located behind NAT, VCMI lobby can operate in "proxy" mode. In this mode, connection will be act as proxy and will transmit gameplay data from client to a match server, without any data processing on lobby server.

Currently, process to establish connection using proxy mode is:

- Player attempt to join open game room using `joinGameRoom` message
- Lobby server validates requests and on success - notifies match server about new player in lobby using control connection
- Match server receives request, establishes new connection to game lobby, sends `serverProxyLogin` message to lobby server and immediately transfers this connection to VCMIServer class to use as connection for gameplay communication
- Lobby server accepts new connection and then sends reply to client using `joinRoomSuccess` message.
- Game client receives message and establishes own side of proxy connection - connects to lobby, sends `clientProxyLogin` message and transfers to ServerHandler class to use as connection for gameplay communication
- Lobby server accepts new connection and moves it into a proxy mode - all packages that will be received by one side of this connection will be re-sent to another side without any processing.

Once the game is over (or if one side disconnects) lobby server will close another side of the connection and erase proxy connection
