#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ockam/syslog.h"
#include "ockam/error.h"
#include "ockam/vault.h"
#include "ockam/transport.h"
#include "handshake.h"

OCKAM_VAULT_CFG_s vault_cfg =
{
		.p_tpm                       = 0,
		.p_host                      = 0,
		OCKAM_VAULT_EC_CURVE25519
};

OCKAM_ERR hex_reader( uint32_t hex, char* buffer, uint16_t buffer_length, uint16_t precision )
{
	OCKAM_ERR status = OCKAM_ERR_NONE;
	if( buffer_length < (precision + 3)) {
		status = OCKAM_ERR_INVALID_SIZE;
		log_error( status, "buffer too small in hex_reader ");
		goto exit_block;
	}
	sprintf( buffer, "0x%0*x", precision, hex );

exit_block:
	return status;
}

OCKAM_ERR initiator_m1_make( HANDSHAKE* p_h, uint8_t* p_prologue, uint16_t prologue_length,
		uint8_t* p_payload, uint16_t payload_length, uint8_t* p_send_buffer, uint16_t buffer_length, uint16_t* p_transmit_size )
{
	printf("\n\n************M1*************\n");

	OCKAM_ERR       status = OCKAM_ERR_NONE;
	uint16_t        buffer_idx = 0;
	uint16_t        transmit_size = 0;

	// Generate a static 25519 keypair for this handshake and get the public key
	status = ockam_vault_key_gen( OCKAM_VAULT_KEY_STATIC );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed to generate static keypair in initiator_step_1");
		goto exit_block;
	}
	status = ockam_vault_key_get_pub( OCKAM_VAULT_KEY_STATIC, p_h->s, KEY_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed to generate get static public key in initiator_step_1");
		goto exit_block;
	}

	// Generate an ephemeral keypair for this transmission and get public key
	status = ockam_vault_key_gen( OCKAM_VAULT_KEY_EPHEMERAL );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed to generate ephemeral keypair in initiator_step_1");
		goto exit_block;
	}
	status = ockam_vault_key_get_pub( OCKAM_VAULT_KEY_EPHEMERAL, p_h->e, KEY_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed to generate get ephemeral public key in initiator_step_1");
		goto exit_block;
	}

	// Nonce to 0, k to empty
	p_h->nonce = 0;
	memset(p_h->k, 0, sizeof(p_h->k));

	// Initialize h to "Noise_XX_25519_AESGCM_SHA256" and set prologue to empty
	memset( &p_h->h[0], 0, SHA256_SIZE );
	memcpy( &p_h->h[0], NAME, NAME_SIZE );
	memset( p_prologue, 0, prologue_length );

	// Initialize ck
	memset( &p_h->ck[0], 0, SHA256_SIZE );
	memcpy( &p_h->ck[0], NAME, NAME_SIZE );

	// h = SHA256(h || prologue), prologue is empty
	status = mix_hash( p_h, p_prologue, prologue_length );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed mix_hash of prologue in initiator_step_1");
		goto exit_block;
	}

	// Write e to outgoing buffer
	// h = SHA256(h || e.PublicKey
	status = mix_hash( p_h, p_h->e, sizeof(p_h->e) );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed mix_hash of e in initiator_step_1");
		goto exit_block;
	}
	if( (buffer_idx + KEY_SIZE) > buffer_length ) {
		status = OCKAM_ERR_INVALID_PARAM;
		log_error( status, "buffer to small in initiator_step_1");
		goto exit_block;
	}
	memcpy( p_send_buffer, p_h->e, KEY_SIZE );
	transmit_size += KEY_SIZE;
	print_uint8_str( p_h->e, KEY_SIZE, "\nM1 e.public: ");

	// Write payload to outgoing buffer, payload is empty
	// h = SHA256( h || payload )
	status = mix_hash( p_h, p_payload, payload_length );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed mix_hash of payload in initiator_step_1");
		goto exit_block;
	}
	memcpy( p_send_buffer, p_payload, payload_length );
	transmit_size += payload_length;

	print_uint8_str( p_send_buffer, KEY_SIZE, "p_m1: ");
	print_uint8_str( p_h->e, KEY_SIZE, "m1 e:  ");

	*p_transmit_size = transmit_size;

exit_block:
	return status;
}

OCKAM_ERR initiator_m2_process( HANDSHAKE* p_h, uint8_t* p_payload, uint32_t payload_size,
		uint8_t* p_recv, uint16_t recv_size )
{
	printf("\n\n************M2*************\n");

	OCKAM_ERR       status = OCKAM_ERR_NONE;
	uint16_t        offset = 0;
	uint8_t         pms[KEY_SIZE];          // pre-master secret
	uint8_t         keys_bytes[2*KEY_SIZE];
	uint8_t         uncipher_text[MAX_TRANSMIT_SIZE];
	uint8_t         tag[TAG_SIZE];
	uint8_t         vector[VECTOR_SIZE];

	// 1. Read 32 bytes from the incoming
	// message buffer, parse it as a public
	// key, set it to re
	// h = SHA256(h || re)
	memcpy( p_h->re, p_recv, KEY_SIZE );
	print_uint8_str( p_h->re, KEY_SIZE, "M2 re");
	offset += KEY_SIZE;
	status = mix_hash( p_h, p_h->re, KEY_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed mix_hash of re in initiator_m2_receive");
		goto exit_block;
	}

	// 2. ck, k = HKDF(ck, DH(e, re), 2)
	// n = 0
	printf("Making k now\n");
	status = hkdf_dh( p_h->ck, sizeof(p_h->ck), OCKAM_VAULT_KEY_EPHEMERAL, p_h->re, sizeof(p_h->re), KEY_SIZE, p_h->ck, p_h->k );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed hkdf_dh of prologue in responder_m2_make");
		goto exit_block;
	}
	print_uint8_str( p_h->k, KEY_SIZE, "M2 k1");

	p_h->nonce = 0;

	// 3. Read 48 bytes of the incoming message buffer as c
	// p = DECRYPT(k, n++, h, c)
	// h = SHA256(h || c),
	// parse p as a public key,
	// set it to rs
	memset( tag, 0, sizeof(tag) );
	memcpy( tag, p_recv+offset+KEY_SIZE, TAG_SIZE );
	make_vector( p_h->nonce, vector );
	print_uint8_str( p_h->k, KEY_SIZE, "M2 decrypt params:\nk: " );
	print_uint8_str( vector, sizeof(vector), "Vector:");
	print_uint8_str( p_h->h, SHA256_SIZE, "h:");
	status = ockam_vault_aes_gcm_decrypt( p_h->k, KEY_SIZE, vector, sizeof(vector),
			p_h->h, sizeof(p_h->h), tag, sizeof(tag), p_recv+offset, KEY_SIZE, uncipher_text, KEY_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed ockam_vault_aes_gcm_decrypt in initiator_m2_recv");
		goto exit_block;
	}
	p_h->nonce += 1;
	memcpy( p_h->rs, uncipher_text, KEY_SIZE );
	print_uint8_str( p_h->rs, KEY_SIZE, "M2 rs (decrypted");
	print_uint8_str( p_recv+offset, KEY_SIZE+TAG_SIZE, "Hashing: ");
	status = mix_hash( p_h, p_recv+offset, KEY_SIZE+TAG_SIZE );

	offset += KEY_SIZE + TAG_SIZE;

	// 4. ck, k = HKDF(ck, DH(e, rs), 2)
	// n = 0
	// secret = ECDH( e, re )
	status = hkdf_dh( p_h->ck,  sizeof(p_h->ck), OCKAM_VAULT_KEY_EPHEMERAL, p_h->rs, sizeof(p_h->rs),
			KEY_SIZE, p_h->ck, p_h->k );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed hkdf_dh of prologue in initiator_m2_process");
		goto exit_block;
	}
	print_uint8_str( p_h->k, KEY_SIZE, "M2 k2:");
	print_uint8_str(p_h->h, SHA256_SIZE, "h");
	p_h->nonce = 0;

	// 5. Read remaining bytes of incoming
	// message buffer as c
	// p = DECRYPT(k, n++, h, c)
	// h = SHA256(h || c),
	// parse p as a payload,
	// payload should be empty
	memset( tag, 0, sizeof(tag) );
	memcpy( tag, p_recv+offset+payload_size, TAG_SIZE );
	print_uint8_str(tag, TAG_SIZE, "M2 decrypt2 tag:");
	make_vector( p_h->nonce, vector );
	print_uint8_str( p_h->k, KEY_SIZE, "M2 decrypt2 params:\nk: " );
	print_uint8_str( vector, sizeof(vector), "Vector:");
	print_uint8_str( p_h->h, SHA256_SIZE, "h:");
	memset(uncipher_text, 0, sizeof(uncipher_text));
	status = ockam_vault_aes_gcm_decrypt( p_h->k, KEY_SIZE, vector, sizeof(vector),
			p_h->h, sizeof(p_h->h), tag, sizeof(tag), p_recv+offset, payload_size, uncipher_text, payload_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed ockam_vault_aes_gcm_decrypt in initiator_m2_recv");
		goto exit_block;
	}
	p_h->nonce += 1;
	print_uint8_str( uncipher_text, 4, "M2 payload");
	mix_hash( p_h, p_recv+offset, TAG_SIZE+payload_size );

exit_block:
	return status;
}

OCKAM_ERR initiator_m3_make( HANDSHAKE* p_h, uint8_t* p_msg, uint16_t max_msg_size,
		uint8_t* p_payload, uint16_t payload_size, uint16_t* p_msg_size )
{
	printf("\n\n************M3*************\n");

	OCKAM_ERR       status = OCKAM_ERR_NONE;
	uint8_t         tag[TAG_SIZE];
	uint8_t         cipher_text[KEY_SIZE];
	u_int16_t       offset = 0;
	uint8_t         vector[VECTOR_SIZE];

	// 1. c = ENCRYPT(k, n++, h, s.PublicKey)
	// h =  SHA256(h || c),
	// Write c to outgoing message
	// buffer, BigEndian
	memset( cipher_text, 0, sizeof(cipher_text) );
	make_vector( p_h->nonce, vector );
	print_uint8_str( p_h->k, KEY_SIZE, "M3 encrypt1 params:\nk: " );
	print_uint8_str( vector, sizeof(vector), "Vector:");
	print_uint8_str( p_h->h, SHA256_SIZE, "h:");
	print_uint8_str(p_h->s, KEY_SIZE, "Encrypting s:");
	status = ockam_vault_aes_gcm_encrypt( p_h->k, KEY_SIZE, vector, sizeof(vector), p_h->h, SHA256_SIZE,
			tag, TAG_SIZE, p_h->s, KEY_SIZE, cipher_text, KEY_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed ockam_vault_aes_gcm_encrypt in initiator_m3_make");
		goto exit_block;
	}
	p_h->nonce += 1;
	memcpy( p_msg, cipher_text, KEY_SIZE );
	offset += KEY_SIZE;
	memcpy( p_msg+offset, tag, TAG_SIZE );
	offset += TAG_SIZE;
	status = mix_hash( p_h, p_msg, KEY_SIZE+TAG_SIZE );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed mix_hash in initiator_m3_make");
		goto exit_block;
	}

	// 2. ck, k = HKDF(ck, DH(s, re), 2)
	// n = 0
	status = hkdf_dh( p_h->ck, sizeof(p_h->ck), OCKAM_VAULT_KEY_STATIC, p_h->re, sizeof(p_h->re),
			KEY_SIZE, p_h->ck, p_h->k );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed hkdf_dh in initiator_m3_make");
		goto exit_block;
	}
	p_h->nonce = 0;
	print_uint8_str(p_h->k, KEY_SIZE, "M3 k1");

	// 3. c = ENCRYPT(k, n++, h, payload)
	// h = SHA256(h || c),
	// payload is empty
	make_vector( p_h->nonce, vector );
	print_uint8_str( p_h->k, KEY_SIZE, "M3 encrypt2 params:\nk: " );
	print_uint8_str( vector, sizeof(vector), "Vector:");
	print_uint8_str( p_h->h, SHA256_SIZE, "h:");
	print_uint8_str(p_payload, payload_size, "encrypting s:");
	status = ockam_vault_aes_gcm_encrypt( p_h->k, KEY_SIZE, vector, sizeof(vector),
			p_h->h, sizeof(p_h->h), &cipher_text[payload_size], TAG_SIZE,
			p_payload, payload_size, &cipher_text[0], payload_size);
	p_h->nonce += 1;
	mix_hash(p_h, cipher_text, payload_size+TAG_SIZE);
	memcpy( p_msg+offset, cipher_text, payload_size+TAG_SIZE );
	print_uint8_str(p_msg, payload_size+TAG_SIZE, "\n\nMsg3:");
	offset += payload_size+TAG_SIZE;
	print_uint8_str(&cipher_text[payload_size], TAG_SIZE, "M3 encrypt 2 tag:");
	// Copy cipher text into send buffer, append tag

	*p_msg_size = offset;

exit_block:
	return status;
}

OCKAM_ERR initiator_epilogue_process( HANDSHAKE* p_h, uint8_t* p_payload, uint32_t payload_size,
                                   uint8_t* p_msg, uint16_t m4_length )
{
	OCKAM_ERR   status		= OCKAM_ERR_NONE;
	uint8_t     uncipher[MAX_TRANSMIT_SIZE];
	uint8_t     tag[TAG_SIZE];
	uint8_t     vector[VECTOR_SIZE];
	uint8_t     keys[2*KEY_SIZE];
	uint32_t    offset = 0;

	printf("\n---------Epilogue----------\n");
	memset(keys, 0, sizeof(keys));
	status = ockam_vault_hkdf( NULL, 0, p_h->ck, KEY_SIZE, NULL, 0, keys, sizeof(keys));
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_vault_hkdf failed in responder_epilogue_make");
	}
	memcpy(p_h->kd, keys, KEY_SIZE );
	memcpy(p_h->ke, &keys[KEY_SIZE], KEY_SIZE );
	p_h->ne = 0;
	p_h->nd = 0;
	print_uint8_str(p_h->kd, KEY_SIZE, "kd");
	print_uint8_str(p_h->ke, KEY_SIZE, "ke");

	memset( tag, 0, sizeof(tag) );
	memcpy( tag, p_msg+offset+payload_size, TAG_SIZE );
	make_vector( p_h->nd, vector );
	print_uint8_str( p_h->k, KEY_SIZE, "M4 decrypt params:\nk: " );
	print_uint8_str(tag, TAG_SIZE, "---tag---");
	print_uint8_str( vector, sizeof(vector), "Vector:");
	memset(uncipher, 0, sizeof(uncipher));
	status = ockam_vault_aes_gcm_decrypt( p_h->kd, KEY_SIZE, vector, sizeof(vector),
	                                      NULL, 0, tag, sizeof(tag), p_msg+offset, payload_size, uncipher, payload_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "failed ockam_vault_aes_gcm_decrypt in initiator_m2_recv");
		goto exit_block;
	}
	memcpy( p_payload, uncipher, payload_size );
	p_h->nd += 1;
	print_uint8_str( uncipher, payload_size, "Epilogue:");

exit_block:
	return status;
}

OCKAM_ERR get_ip_info( OCKAM_INTERNET_ADDRESS* p_address )
{

	OCKAM_ERR   status		= OCKAM_ERR_NONE;
	FILE*       address_file;
	char        listen_address[100];
	char        port_str[8];
	unsigned    port = 0;

	// Read the IP address to bind to
	address_file = fopen("../ipaddress.txt", "r");
	if(NULL == address_file) {
		printf("Create a file called \"ipaddress.txt\" with the IP address to listen on," \
			"in nnn.nnn.nnn.nnn format and port number\n");
		status = OCKAM_ERR_INVALID_PARAM;
		goto exit_block;
	}
	fscanf(address_file, "%s\n", &listen_address[0]);
	fscanf(address_file, "%s\n", &port_str[0]);
	port = strtoul( &port_str[0], NULL, 0 );
	fclose(address_file);

	memset( p_address, 0, sizeof( *p_address));

	strcpy( &p_address->ip_address[0], &listen_address[0] );
	p_address->port = port;

exit_block:
	return status;
}

OCKAM_ERR establish_connection( OCKAM_TRANSPORT_CONNECTION* p_connection )
{
	OCKAM_ERR                       status = OCKAM_ERR_NONE;
	OCKAM_INTERNET_ADDRESS          responder_address;
	OCKAM_TRANSPORT_CONNECTION      connection = NULL;

	// Get the IP address of the responder
	status = get_ip_info( &responder_address );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "failed to get address into");
		goto exit_block;
	}

	status = ockam_init_posix_tcp_connection( &connection );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "failed ockam_init_posix_tcp_connection");
		goto exit_block;
	}
	// Try to connect
	status = ockam_connect_blocking( &responder_address, connection );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "connect failed" );
		goto exit_block;
	}

	*p_connection = connection;

exit_block:
	return status;
}

int main() {
	OCKAM_ERR                       status = OCKAM_ERR_NONE;
	OCKAM_TRANSPORT_CONNECTION      connection;
	HANDSHAKE                       handshake;
	uint8_t                         prologue[4];
	uint8_t                         p_in[4];
	uint16_t                        p_in_size = 4;
	uint8_t                         p_out[4] = {10, 11, 12, 13};
	uint16_t                        p_out_size = 4;
	uint8_t                         send_buffer[MAX_TRANSMIT_SIZE];
	uint8_t                         recv_buffer[MAX_TRANSMIT_SIZE];
	uint16_t                        bytes_received = 0;
	uint16_t                        transmit_size = 0;
	uint8_t                         epi[EPI_BYTE_SIZE];


	init_err_log(stdout);

	/*-------------------------------------------------------------------------
	 * Establish transport connection with responder
	 *-----------------------------------------------------------------------*/

	status = establish_connection( &connection );
	if( OCKAM_ERR_NONE != status ) {
		log_error(status, "Failed to establish connection with responder");
		goto exit_block;
	}

	status = ockam_vault_init((void*) &vault_cfg);                 /* Initialize vault                                   */
	if(status != OCKAM_ERR_NONE) {
		log_error( status, "ockam_vault_init failed" );
		goto exit_block;
	}

	// Step 1 generate message
	status = initiator_m1_make( &handshake,  NULL, 0, NULL, 0, &send_buffer[0], MAX_TRANSMIT_SIZE, &transmit_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "initiator_step_1 failed" );
		goto exit_block;
	}

	// Step 1 send message
	status = ockam_send_blocking( connection, send_buffer, transmit_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_send_blocking after initiator_step_1 failed" );
		goto exit_block;
	}

	// Msg 2 receive
	status = ockam_receive_blocking( connection, recv_buffer, sizeof(recv_buffer), &bytes_received );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_receive_blocking failed on msg 2" );
		goto exit_block;
	}

	// Msg 2 process
	status = initiator_m2_process( &handshake, p_in, p_in_size,
	                               recv_buffer, bytes_received );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_receive_blocking failed on msg 2" );
		goto exit_block;
	}

	// Msg 3 make
	status = initiator_m3_make( &handshake, send_buffer, sizeof( send_buffer ),
			p_out, p_out_size,  &transmit_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "initiator_m3_make failed" );
		goto exit_block;
	}
	// Msg 3 send
	status = ockam_send_blocking( connection, send_buffer, transmit_size );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_send_blocking failed on msg 3" );
		goto exit_block;
	}
	print_uint8_str( send_buffer, transmit_size, "Msg 3 sent: ");

	// Epilogue receive
	status = ockam_receive_blocking( connection, recv_buffer, sizeof(recv_buffer), &bytes_received );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_receive_blocking failed on msg 2" );
		goto exit_block;
	}

	// Epilogue process
	status = initiator_epilogue_process( &handshake, epi, EPI_BYTE_SIZE, recv_buffer, bytes_received );
	if( OCKAM_ERR_NONE != status ) {
		log_error( status, "ockam_receive_blocking failed on msg 2" );
		goto exit_block;
	}

	// Epilogue make

exit_block:
	return status;
}
