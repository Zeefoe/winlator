#include <stdio.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MIN_REQUEST_LENGTH 5

#define REQUEST_CODE_CLOSE 0
#define REQUEST_CODE_START 1
#define REQUEST_CODE_STOP 2
#define REQUEST_CODE_PAUSE 3
#define REQUEST_CODE_PREPARE 4
#define REQUEST_CODE_WRITE 5
#define REQUEST_CODE_DRAIN 6
#define REQUEST_CODE_POINTER 7

#define DATA_TYPE_U8 0
#define DATA_TYPE_S16LE 1
#define DATA_TYPE_S16BE 2
#define DATA_TYPE_FLOATLE 3
#define DATA_TYPE_FLOATBE 4

#define ARRAY_SIZE(arr)	(sizeof(arr) / sizeof(arr[0]))

typedef struct snd_pcm_android_aserver {
	snd_pcm_ioplug_t io;
	int fd;
	int frame_bytes;
	unsigned int position;
	char* buffer;
	int buffer_size;
} snd_pcm_android_aserver_t;

static void grow_buffer_if_necessary(snd_pcm_android_aserver_t* android_aserver, int new_size) {
	if (new_size > android_aserver->buffer_size) {
		if (android_aserver->buffer) free(android_aserver->buffer);
		android_aserver->buffer_size = new_size;
		android_aserver->buffer = (char*)malloc(new_size);
	}
}

static int android_aserver_close(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	if (!android_aserver) return 0;
		
	if (android_aserver->fd >= 0) {
		int request_length = 0;
		char request_data[MIN_REQUEST_LENGTH];
		request_data[0] = REQUEST_CODE_CLOSE;
		memcpy(request_data + 1, &request_length, 4);
	
		write(android_aserver->fd, request_data, sizeof(request_data));
		close(android_aserver->fd);
	}
	
	if (android_aserver->buffer) {
		free(android_aserver->buffer);
		android_aserver->buffer_size = 0;
	}
	
	free(android_aserver);
	return 0;
}

static int android_aserver_start(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;

	grow_buffer_if_necessary(android_aserver, MIN_REQUEST_LENGTH);
	
	int request_length = 0;
	android_aserver->buffer[0] = REQUEST_CODE_START;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, MIN_REQUEST_LENGTH);
	if (res < 0) return -EINVAL;
	
	return 0;
}

static int android_aserver_stop(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	
	grow_buffer_if_necessary(android_aserver, MIN_REQUEST_LENGTH);

	int request_length = 0;
	android_aserver->buffer[0] = REQUEST_CODE_STOP;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, MIN_REQUEST_LENGTH);
	if (res < 0) return -EINVAL;
	
	return 0;
}

static int android_aserver_pause(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	
	grow_buffer_if_necessary(android_aserver, MIN_REQUEST_LENGTH);

	int request_length = 0;
	android_aserver->buffer[0] = REQUEST_CODE_PAUSE;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, MIN_REQUEST_LENGTH);
	if (res < 0) return -EINVAL;
	
	return 0;
}

static int android_aserver_prepare(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	android_aserver->frame_bytes = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;
	android_aserver->position = 0;
	
	char channels = (char)io->channels;
	char data_type;
	switch (io->format) {
		case SND_PCM_FORMAT_U8:
			data_type = DATA_TYPE_U8;
			break;
		case SND_PCM_FORMAT_S16_LE:
			data_type = DATA_TYPE_S16LE;
			break;
		case SND_PCM_FORMAT_S16_BE:
			data_type = DATA_TYPE_S16BE;
			break;
		case SND_PCM_FORMAT_FLOAT_LE:
			data_type = DATA_TYPE_FLOATLE;
			break;
		case SND_PCM_FORMAT_FLOAT_BE:
			data_type = DATA_TYPE_FLOATBE;
			break;			
		default:
			return -EINVAL;			
	}
	
	int request_length = 10;
	grow_buffer_if_necessary(android_aserver, request_length + MIN_REQUEST_LENGTH);
	android_aserver->buffer[0] = REQUEST_CODE_PREPARE;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	memcpy(android_aserver->buffer + 5, &channels, 1);
	memcpy(android_aserver->buffer + 6, &data_type, 1);
	memcpy(android_aserver->buffer + 7, &io->rate, 4);
	memcpy(android_aserver->buffer + 11, &io->buffer_size, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, request_length + MIN_REQUEST_LENGTH);
	if (res < 0) return -EINVAL;
	
	return 0;
}

static snd_pcm_sframes_t android_aserver_pointer(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	
	grow_buffer_if_necessary(android_aserver, MIN_REQUEST_LENGTH);

	int request_length = 0;
	android_aserver->buffer[0] = REQUEST_CODE_POINTER;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, MIN_REQUEST_LENGTH);
	if (res < 0) return android_aserver->position;
	
	int position;
	res = read(android_aserver->fd, &position, 4);
	if (res < 0) return android_aserver->position;
	
	android_aserver->position = position;
	return android_aserver->position;
}

static snd_pcm_sframes_t android_aserver_write(snd_pcm_ioplug_t* io, const snd_pcm_channel_area_t* areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;

	const char* src_data = (char*)areas->addr + (areas->first + areas->step * offset) / 8;

	int request_length = size * android_aserver->frame_bytes;
	grow_buffer_if_necessary(android_aserver, request_length + MIN_REQUEST_LENGTH);
	
	android_aserver->buffer[0] = REQUEST_CODE_WRITE;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	memcpy(android_aserver->buffer + 5, src_data, request_length);
	
	int res = write(android_aserver->fd, android_aserver->buffer, request_length + MIN_REQUEST_LENGTH);
	return (res >= MIN_REQUEST_LENGTH ? res - MIN_REQUEST_LENGTH : 0) / android_aserver->frame_bytes;
}

static int android_aserver_drain(snd_pcm_ioplug_t* io) {
	snd_pcm_android_aserver_t* android_aserver = io->private_data;
	
	grow_buffer_if_necessary(android_aserver, MIN_REQUEST_LENGTH);

	int request_length = 0;
	android_aserver->buffer[0] = REQUEST_CODE_DRAIN;
	memcpy(android_aserver->buffer + 1, &request_length, 4);
	
	int res = write(android_aserver->fd, android_aserver->buffer, MIN_REQUEST_LENGTH);
	if (res < 0) return -EINVAL;
	
	return 0;
}

static int android_aserver_set_hw_constraint(snd_pcm_ioplug_t* io) {	
    static const unsigned int access_list[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
    static const unsigned int format_list[] = {SND_PCM_FORMAT_U8, SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S16_BE, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_FORMAT_FLOAT_BE};
    int err;

    err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS, ARRAY_SIZE(access_list), access_list);
    if (err < 0) return err;

    err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT, ARRAY_SIZE(format_list), format_list);
    if (err < 0) return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS, 1, 2);
    if (err < 0) return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE, 8000, 48000);
    if (err < 0) return err;
	
	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 64, 64 * 1024);
	if (err < 0) return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64);
	if (err < 0) return err;	
    return 0;
}

static snd_pcm_ioplug_callback_t android_aserver_callback = {
	.close = android_aserver_close,
	.start = android_aserver_start,
	.stop = android_aserver_stop,
	.pause = android_aserver_pause,
	.prepare = android_aserver_prepare,
	.transfer = android_aserver_write,
	.drain = android_aserver_drain,
	.pointer = android_aserver_pointer,
};

static int android_aserver_connect() {
	char* path = getenv("ANDROID_ALSA_SERVER");
	
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
	
	struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
	
	strncpy(server_addr.sun_path, path, sizeof(server_addr.sun_path) - 1);
	
	int res = connect(fd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_un));
	if (res < 0) {
		close(fd);
		return -1;
	}

	return fd;		
}

static int android_aserver_create(snd_pcm_t** pcmp, const char* name, snd_pcm_stream_t stream, int mode) {
	snd_pcm_android_aserver_t* android_aserver;
	
	if (stream != SND_PCM_STREAM_PLAYBACK) return -ENOTSUP;
	
	android_aserver = calloc(1, sizeof(snd_pcm_android_aserver_t));
	if (!android_aserver) return -ENOMEM;
	
	android_aserver->buffer_size = 0;
	android_aserver->io.version = SND_PCM_IOPLUG_VERSION;
	android_aserver->io.name = "ALSA <-> Android AServer PCM Plugin";
	android_aserver->io.callback = &android_aserver_callback;
	android_aserver->io.mmap_rw = 0;
	android_aserver->io.private_data = android_aserver;
	
	int res = -EINVAL;
	android_aserver->fd = android_aserver_connect();
	if (android_aserver->fd < 0) goto error;
	
	res = snd_pcm_ioplug_create(&android_aserver->io, name, stream, mode);
	if (res < 0) goto error;
	
	res = android_aserver_set_hw_constraint(&android_aserver->io);
    if (res < 0) {
		snd_pcm_ioplug_delete(&android_aserver->io);
		goto error;
    }
	
	*pcmp = android_aserver->io.pcm;
	return 0;
	
	error:
	if (android_aserver->fd >= 0) close(android_aserver->fd);
	free(android_aserver);
	return res;	
}

SND_PCM_PLUGIN_DEFINE_FUNC(android_aserver) {
	snd_config_iterator_t i, next;
	
	snd_config_for_each(i, next, conf) {
		snd_config_t* n = snd_config_iterator_entry(i);
		const char* id;
		
		if (snd_config_get_id(n, &id) < 0) continue;
		if (strcmp(id, "type") == 0 || strcmp(id, "hint") == 0) continue;
		
		return -EINVAL;
	}
	
	int err = android_aserver_create(pcmp, name, stream, mode);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(android_aserver);