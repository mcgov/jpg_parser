/* A decoder to look for bytes in jpeg */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define SHORT(x) ntohs((x))
#define LONG(x)  ntohl((x))


bool SOI, SOS, EOI, JFIF_APPO, JFXX_APPO;
bool* bools[] = { &SOI, &SOS, &EOI, &JFIF_APPO, &JFXX_APPO, NULL };

void xor_set(bool* pointer)
{
	 bool** ptr = bools;
	 while ( (*ptr) != NULL )
	 {
			if ((*ptr) == pointer) {(**ptr) = true;}
			else {(**ptr) = false;}
			ptr++;
	 }
}
void toggle_single(bool* pointer)
{
	 bool** ptr = bools;
	 while ( (*ptr) != NULL )
	 {
			if ((*ptr) == pointer) {(**ptr) = true;}
			ptr++;
	 }
}

void _SOI(){
	if(!SOI)
	{
		printf("start of image\n");
		xor_set(&SOI);
	}
}

struct scan_section_head{
	uint16_t length;
	uint8_t n_components;
	uint16_t data[0];
};
struct scan_section_tail{
	uint8_t end_magic[3];
	//0, 63, 0
};


void _SOS(int fd)
{
	struct scan_section_head sh;
	struct scan_section_tail st;
	printf("start of scan\n");
	if ( read(fd,&sh,3) )
	{
		sh.length = SHORT(sh.length);
		printf("Scan section length is %hu\n", sh.length);
		printf("Scan section components %hhu\n", sh.n_components);
		lseek(fd,sh.n_components*2, SEEK_CUR);
		read(fd,&st,3);
		printf("Scan Section end magic (should be 0 63 0) %hhu %hhu %hhu\n",
			st.end_magic[0], st.end_magic[1], st.end_magic[2]);
	}
	xor_set(&SOS);

}
void _EOI(){
	xor_set(&EOI);
	printf("\nend of image\n");
}
void _JFXX_APPO(){
		toggle_single(&JFXX_APPO);
		printf("JFXX APPO tag\n");
}

struct rgbdata{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct JFIF_appo_header{
	uint16_t length;
	char indentifier[5];
	uint8_t major_v;
	uint8_t minor_v;
	uint8_t density;
	uint16_t xdensity;
	uint16_t ydensity;
	uint8_t xthumbnail;
	uint8_t ythumbnail;
	struct rgbdata thumbnail_data[0];
};

struct sof_data{
	uint16_t length;
	uint8_t data[0];
};



void print_JFIF_header(struct JFIF_appo_header* h )
{
	printf(
		"length: %hd\n"
		"ident: %s\n"
		"major version: %hhu\n"
		"minor version: %hhu\n"
		"density: %hhu\n"
		"xdens:  %hu\n"
		"ydens: %hu\n"
		"xthumb: %hhu\n"
		"ythumb: %hhu\n",
		SHORT(h->length), h->indentifier, h->major_v, h->minor_v,
		h->density, SHORT(h->xdensity), SHORT(h->ydensity), h->xthumbnail, h->ythumbnail );


}



void _JFIF_APPO(int fd){
	struct JFIF_appo_header header;;
	int read_count = read(fd, &header, sizeof(header));
	print_JFIF_header(&header);
	uint32_t thumbnail_size = sizeof(struct rgbdata) * header.xthumbnail * header.ythumbnail;

	//for now just skip
	lseek(fd,thumbnail_size,SEEK_CUR);

}
void _APPO(int fd){
if ( SOI || JFIF_APPO){
		_JFIF_APPO(fd);
	}
	else if (SOI && JFIF_APPO )
	{
		_JFXX_APPO();			
	}
}

void _COMMENT(int fd){
	printf("Comment found\n");
}

struct APPN_data{
	uint16_t length;
	uint8_t data[0];
};

void _APPN(int fd, uint8_t byte)
{
	printf("APP%hhu Tag found\n", byte & 0x0F );
	struct APPN_data appn = {0};
	if ( read(fd, &appn, sizeof(appn) ) ){
		appn.length = SHORT(appn.length);
		printf("APPN section is length%04hx\n",appn.length );
		int i = 0;
		uint16_t section_size = appn.length - sizeof(appn);
		uint8_t* data = malloc(section_size+1);
		read(fd,data,section_size);
		while ( i < section_size) {
			printf("%02hhx ", data[i++]);
			if (i % 16 == 0){
				printf("\n");
			}
		}
		printf("\n");
		i = 0 ;
		while ( i < section_size) {
			printf("%c", data[i++]);
		}
		printf("\n");
	} else{
		exit(-1);
	}

}

uint16_t restart_interval = 0;

void _DRI(int fd){
	//define restart interval tag;
	printf("found define restart interval tag\n");
	int read_bytes = read(fd, &restart_interval, 2);
	if (!read_bytes)
		exit(-1);

	restart_interval = SHORT(restart_interval);
	

}

struct HuffmanTable{
	uint16_t length;
	uint8_t ht_info;
	uint8_t n_symbols[16];
	uint8_t data[0];
};

struct QuantizationTable{
	uint16_t length;
	uint8_t wordsize_dest;
	uint8_t data[0];
};


void _DHT(int fd)
{
	struct HuffmanTable ht;
	printf("HuffmanTable: ");
	if (read(fd,&ht,sizeof(ht)) ){
		int i = 0, sum = 0;
		printf("adding number of codes up...");
		
		while ( i < 16 )
		{
			sum += ht.n_symbols[i];
			i++;
		}
		printf("found %d symbols\n", sum);
		lseek(fd,sum-1,SEEK_CUR);
	} else{
		printf("\n");
		exit(-1);
	}

}


struct sof0_data{
	uint16_t length;
	uint8_t precision;
	uint16_t image_height;
	uint16_t image_width;
	uint8_t number_of_components; // 1bw, 3 YcbCr 4 CMYK
	uint8_t data[0];
};

void _SOF0(int fd){
	printf("SOF section...\n");
	struct sof0_data sof0;
	if (read(fd,&sof0,sizeof(sof0)) ){
		printf("SOF0 section reported length%hu\n", SHORT(sof0.length)-2);
		lseek(fd,SHORT(sof0.length)-sizeof(sof0),SEEK_CUR);
	} else{
		exit(-1);
	}
}

void _SOF2(int fd){
	struct sof_data sof2;
	printf("SOF section...\n");
	if (read(fd,&sof2,sizeof(sof2)) ){
		printf("SOF2 section is length%hu\n", SHORT(sof2.length));
		lseek(fd,SHORT(sof2.length)-sizeof(sof2),SEEK_CUR);
	} else{
		exit(-1);
	}
}


void _DQT(int fd)
{
	struct QuantizationTable qt;
	printf("DQT: ");
	if (read(fd,&qt,sizeof(qt)) ){
		qt.length = SHORT(qt.length);
		printf("quantization table reported length%hu\n", qt.length);
		int byte_or_word = qt.wordsize_dest | 0xF0;
		int table_size = 64;
		if (byte_or_word == 1)
		{
			printf("QuantizationTable is in words!\n");
			table_size *= 2;
		}
		else{
			printf("QuantizationTable is in bytes\n");
		}
		lseek(fd, table_size-1 ,SEEK_CUR);
	} else{
		printf("\n");
		exit(-1);
	}
}

int main(int argc, char** argv)
{	
	SOI = SOS = EOI = JFIF_APPO = JFXX_APPO = false;

	if ( argc < 2 )
	{
		printf("Missing open(filename) argument");
		exit(0);
	}
	printf( "attempting to open %s read only\n",argv[1]);
	int fd = open(argv[1], 'r');
	


	int row_align = 0;
	int read_byte; uint8_t byte; uint8_t peek;
	while ( read(fd, &byte, 1) )
	{
		bool print_it = true;
		printf("%02hhx ", byte);
		if ( byte == 0xff )
		{
			if (read(fd,&byte,1) )
			{
				
				switch ( byte )
				{
					case 0xd8:
						_SOI();
						break;
					case 0xda:
						_SOS(fd);
						break;
					case 0xd9:
						_EOI();
						print_it = false;
						break;
					case 0xe0:
						_APPO(fd);
						break;
					case 0xe1:
					case 0xe2:
					case 0xe3:
					case 0xe4:
					case 0xe5:
					case 0xe6:
					case 0xe7:
					case 0xe8:
						_APPN(fd, byte);
						break;
					case 0xFE:
						_COMMENT(fd);
						break;
					case 0xDD:
						_DRI(fd);

					//restart interval tags
					case 0xd0:
					case 0xd1:
					case 0xd2:
					case 0xd3:
					case 0xd4:
					case 0xd5:
					case 0xd6:
					case 0xd7:
						break;
					case 0xDB:
						_DQT(fd);
						break;
					case 0xC4:
						_DHT(fd);
						break;
					case 0xC0:
						_SOF0(fd);
						break;
					case 0xC2:
						_SOF2(fd);
						break;

					default:
						if (!SOS){
								printf("Found unknown tag FF%02hhx\n",byte);
						} 
				}

			
			}
		}	
		if (SOS && print_it)
		{
			printf("%02hhx ", byte);
			if ( ++row_align % 10 == 0 )
				printf("\n");

		}
		
	}

	close(fd);	
}
