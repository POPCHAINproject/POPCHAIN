Protocol Documentation - 0.12.1
=====================================

This document describes the protocol extensions for all additional functionality build into the Pop protocol. This doesn't include any of the Bitcoin procotol, which has been left in tact in the Pop project. For more information about the core protocol, please see https://en.bitcoin.it/w/index.php?title#Protocol_documentation&action#edit

## Common Structures

### Simple types

uint256  => char[32]

CScript => uchar[]

### COutPoint

Bitcoin Outpoint https://bitcoin.org/en/glossary/outpoint

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 32 | hash | uint256 | Hash of transactional output which is being referenced
| 4 | n | uint32_t | Index of transaction which is being referenced


### CTxIn

Bitcoin Input https://bitcoin.org/en/glossary/input

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 36 | prevout | COutPoint | The previous output from an existing transaction, in the form of an unspent output
| 1+ | script length | var_int | The length of the signature script
| ? | script | CScript | The script which is validated for this input to be spent
| 4 | nSequence | uint_32t | Transaction version as defined by the sender. Intended for "replacement" of transactions when information is updated before inclusion into a block.

### CTxOut

Bitcoin Output https://bitcoin.org/en/glossary/output

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 8 | nValue | int64_t | Transfered value
| ? | scriptPubKey | CScript | The script for indicating what conditions must be fulfilled for this output to be further spent

### CPubKey

Bitcoin Public Key https://bitcoin.org/en/glossary/public-key

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 33-65 | vch | char[] | The public portion of a keypair which can be used to verify signatures made with the private portion of the keypair.

### DSTX - "dstx"

CDarksendBroadcastTx

Popnodes can broadcast subsidised transactions without fees for the sake of security in mixing. This is done via the DSTX message.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| # | tx | CTransaction | The transaction
| 41 | vin | CTxIn | Popnode unspent output
| 71-73 | vchSig | char[] | Signature of this message by popnode (verifiable via pubKeyPopnode)
| 8 | sigTime | int64_t | Time this message was signed

### DSSTATUSUPDATE - "dssu"

Mixing pool status update

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nMsgSessionID | int | Session ID
| 4 | nMsgState | int | Current state of mixing process
| 4 | nMsgEntriesCount | int | Number of entries in the mixing pool
| 4 | nMsgStatusUpdate | int | Update state and/or signal if entry was accepted or not
| 4 | nMsgMessageID | int | ID of the typical popnode reply message

### DSQUEUE - "dsq"

CDarksendQueue

Asks users to sign final mixing tx message.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nDenom | int | Which denomination is allowed in this mixing session
| 41 | vin | CTxIn | unspend output from popnode which is hosting this session
| 4 | nTime | int | the time this DSQ was created
| 4 | fReady | int | if the mixing pool is ready to be executed
| 71-73 | vchSig | char[] | Signature of this message by popnode (verifiable via pubKeyPopnode)

### DSACCEPT - "dsa"

Response to DSQ message which allows the user to join a mixing pool

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nDenom | int | denomination that will be exclusively used when submitting inputs into the pool
| 41+ | txCollateral | int | collateral tx that will be charged if this client acts maliciousely

### DSVIN - "dsi"

CDarkSendEntry

When queue is ready user is expected to send his entry to start actual mixing

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| ? | vecTxDSIn | CTxDSIn[] | vector of users inputs (CTxDSIn serialization is equal to CTxIn serialization)
| 8 | nAmount | int64_t | depreciated since 12.1, it's used for backwards compatibility only and can be removed with future protocol bump
| ? | txCollateral | CTransaction | Collateral transaction which is used to prevent misbehavior and also to charge fees randomly
| ? | vecTxDSOut | CTxDSOut[] | vector of user outputs (CTxDSOut serialization is equal to CTxOut serialization)

### DSSIGNFINALTX - "dss"

User's signed inputs for a group transaction in a mixing session


| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| # | inputs | CTxIn[] | signed inputs for mixing session
