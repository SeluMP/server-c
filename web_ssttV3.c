#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VERSION		24
#define BUFSIZE		8096
#define COOKIE_BUFSIZE 128
#define ERROR		42
#define LOG			44
#define BAD_REQUEST 400
#define PROHIBIDO	403
#define NOENCONTRADO	404
#define PERSISTENCE_TIME 15
#define DEFAULT_ADDR "./index.html"
#define NOTALLOWED	405
#define MAXCOOKIE	10
#define HTTP_VERSION_ERROR 505
#define UNSUPPORTED			415
#define TOO_MANY_REQUESTS	429
#define OK			200

#define HTML_1 "<HTML> <HEAD> <TITLE>"
#define HTML_2 "</TITLE> </HEAD><BODY> <H1>"
#define HTML_3 "</H1><HR><ADDRESS>Servidor HTTP - SSTT</ADDRESS></BODY> </HTML>"

int cookie_counter = -1;
struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

struct RequestHTTP {
	char *method;
	char *url;
	char *version;
};

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}

void parse_request(char *line, struct RequestHTTP *req){
	char buf[BUFSIZE];
	char aux[128];
	char *aux2;
	char *p;
	char *save_p;

	strcpy(buf, line);
	

	p = strtok_r(line," ", &save_p);
	req->method = p;

	p = strtok_r(NULL, " ", &save_p);
	if(!strcmp(p,"/")){
		req->url = DEFAULT_ADDR;	
	}
	else {
		strcpy(aux,".");
		strcat(aux,p);
		req->url = aux;
	}
	p = strtok_r(NULL, "\r", &save_p);
	req->version = p;	
}

char* date_string(int add_mins){
	char buffer[128];
	time_t tiempo = time(&tiempo);
	tiempo += add_mins * 60; 	// Pasamos minutos a segundos e incrementamos
	struct tm *tm = gmtime(&tiempo);

	strftime(buffer, 128, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", tm);
	return strdup(buffer);
}

char * allowed_extension(struct RequestHTTP req){
	req.url = strrchr(req.url, '.');
	if(!req.url) return NULL;
	req.url++; // Avanzamos a la posición destrás del punto
	int i = 0;
	while (extensions[i].ext != 0){
		if(strcmp(extensions[i].ext,req.url) == 0){
			return strdup(extensions[i].filetype);
		}
		i++;
	}
	return NULL;
}


char * create_cookie() {
	char cookie[COOKIE_BUFSIZE];
	if(cookie_counter < 0){
		sprintf(cookie, "Set-Cookie: cookie_counter=1; Max-Age=120\r\n");

	}
	else if (cookie_counter < MAXCOOKIE){
		sprintf(cookie, "Set-Cookie: cookie_counter=%d; Max-Age=120\r\n", ++cookie_counter);	
	}
	else {
		return NULL;
	}
	return strdup(cookie);
}

// Función utilizada para crear las respuestas, tanto las correctas como los mensajes de error
void create_errorMessage(int type, int descriptorFichero, char * extension,int fd){
	char *response = malloc(BUFSIZE);
	char *body 	   = malloc(BUFSIZE);
	int indice;
	int indice_msg = 0;
	char * file = malloc(512);

	char * cookie = create_cookie();
	if(cookie == NULL){
		type = TOO_MANY_REQUESTS;
	}
	switch(type){
		case OK:
			indice = sprintf(response,"HTTP/1.1 200 OK\r\n");
			break;	
		case BAD_REQUEST:
			indice = sprintf(response, "%s", "HTTP/1.1 400 Bad Request\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"400 BAD REQUEST",HTML_2,"Error occurred: 400 Bad Request",HTML_3);
			extension = "text/html";
			break;
		case PROHIBIDO:
			indice = sprintf(response, "%s", "HTTP/1.1 403 Forbidden\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"403 FORBIDDEN",HTML_2,"Error occurred: 403 Forbidden",HTML_3);
			extension = "text/html";
			break;
		case NOENCONTRADO:
			indice = sprintf(response, "%s", "HTTP/1.1 404 Not Found\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"404 NOT FOUND",HTML_2,"Error occurred: 404 Not Found",HTML_3);
			extension = "text/html";
			break;
		case NOTALLOWED:
			indice = sprintf(response, "%s", "HTTP/1.1 405 Method Not Allowed\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"405 METHOD NOT ALLOWED",HTML_2,"Error occurred: 405 Method Not Allowed",HTML_3);
			extension = "text/html";
			break;
		case UNSUPPORTED:
			indice = sprintf(response, "HTTP/1.1 415 Unsupported Media Type\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"415 UNSUPPORTED MEDIA TYPE",HTML_2,"Error occurred: 415 Unsupported Media Type",HTML_3);
			extension = "text/html";
			break;
		case TOO_MANY_REQUESTS:
			indice = sprintf(response, "%s", "HTTP/1.1 429 Too Many Requests\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"429 TOO MANY REQUEST",HTML_2,"Error occurred: 429 Too Many Request",HTML_3);
			extension = "text/html";
			break;
		case HTTP_VERSION_ERROR:
			indice = sprintf(response, "%s", "HTTP/1.1 505 HTTP Version Not Supported\r\n");
			indice_msg = sprintf(file, "%s %s %s %s %s",HTML_1,"505 HTTP VERSION NOT SUPPORTED",HTML_2,"Error occurred: 505 HTTP Version Not Supported",HTML_3);
			extension = "text/html";
			break;
	}
	char* time_c = date_string(0);
	indice += sprintf(response + indice, "%s", time_c);
	indice += sprintf(response + indice, "Server: www.sstt6059.org\r\n");
	if(indice_msg > 0){
		indice += sprintf(response + indice, "Content-Length: %d\r\n",(int) strlen(file)); 
	}
	else{
		struct stat o_file;
		fstat(fd, &o_file);
		indice += sprintf(response + indice, "Content-Length: %ld\r\n",o_file.st_size);
	}
	indice += sprintf(response + indice, "Content-Type: %s\r\n", extension);
	indice += sprintf(response + indice, "Connection: keep-alive\r\n");
	indice += sprintf(response + indice, "Keep-Alive: %d\r\n",PERSISTENCE_TIME);
	if(cookie != NULL) indice += sprintf(response + indice, "%s", cookie); 
	indice += sprintf(response + indice, "\r\n");
	


	//
	//	En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
	//	correspondiente, y el envio del fichero se hace en blockes de un máximo de  8kB
	//

	debug(LOG, "Enviamos un mensaje tipo", response, descriptorFichero);
	write(descriptorFichero, response, indice); // Se escribe el contenido de 'response'

	/* Leemos el fichero con el contenido de la respuesta y lo escribimos en el socket */ 

	if(indice_msg > 0){
			write(descriptorFichero, file, strlen(file));
	}
	else{
		int read_bytes;
		while ((read_bytes = read(fd, body, BUFSIZE)) > 0) {
			write(descriptorFichero, body, read_bytes);
		}
	}
	
}

// Función encargada de mantener la persistencia
int persistence(int descriptorFichero, long int seg, long int useg){
	struct timeval tv;
	fd_set rfds;
	tv.tv_sec = seg;
	tv.tv_usec = useg;
	FD_ZERO(&rfds);
	FD_SET(descriptorFichero, &rfds);
	if (select(descriptorFichero + 1, &rfds, NULL, NULL, &tv) < 0) {
		perror("select()");
		exit(EXIT_FAILURE);
	}
	/* Se comprueba si hay elementos por leer aún */
	return (FD_ISSET(descriptorFichero, &rfds));
}

void process_web_request(int descriptorFichero)
{
	while(persistence(descriptorFichero,PERSISTENCE_TIME,0)){

		debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
		//
		// Definir buffer y variables necesarias para leer las peticiones
		//
		char  buffer [BUFSIZE];
		struct RequestHTTP request;
		
		char * exten;
		char * path;
		int  df;
		//
		// Leer la petición HTTP
		//
		ssize_t n_bytes = read(descriptorFichero, buffer, BUFSIZE);
		
		//
		// Comprobación de errores de lectura
		//
		if(n_bytes < 0){
			debug(ERROR, "Error read_request", "Fallo en la lectura de la petición", descriptorFichero);
			close(descriptorFichero);
			exit(EXIT_FAILURE);
		}
		
		//
		// Rellenamos la estructura request con los datos de la petición
		//
		char * token = strtok(buffer, "\r\n"); 	// Línea inicial -> GET / HTTP/1.X 
		parse_request(token, &request);
		token = strtok(NULL, "\r\n");

		// Lectura del resto de cabeceras
		while(token){
			debug(LOG, "Sección cabecera parseada: ", token, descriptorFichero);

			// Tratamos el caso de encontrarnos una cookie
			if(strncmp(token, "Cookie", 6) == 0){
				char cookieToken[BUFSIZE];
				char *p;
				char *save_p;
				strcpy(cookieToken, token);
				p = strtok_r(cookieToken, "=\r\n", &save_p); 	// Obtenemos la línea con la cookie
				p = strtok_r(NULL, "=\r\n", &save_p);			// Obtenemos el valor del counter
				cookie_counter = atoi(p);
			}
			token = strtok(NULL, "\r\n");
		}
		//
		//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
		//	(Se soporta solo GET)
		//
		if(strcmp(request.method, "GET") != 0) {
			create_errorMessage(NOTALLOWED,descriptorFichero,"",0);
		}
		else if(strcmp(request.version, "HTTP/1.1") != 0){
			create_errorMessage(HTTP_VERSION_ERROR,descriptorFichero,"",0);
		}
		else if(strstr(request.url, "../") != NULL){
			create_errorMessage(PROHIBIDO,descriptorFichero,"",0); 		//	Como se trata el caso de acceso ilegal a directorios superiores de la jerarquia de directorios del sistema
		}
		else{
				exten = allowed_extension(request);
				df = open(request.url,O_RDONLY);
				int type;

				if(exten == NULL){
					type = UNSUPPORTED;			// Evaluar el tipo de fichero que se está solicitando, y actuar en consecuencia devolviendolo si se soporta u devolviendo el error correspondiente en otro caso
				}
				else if (df < 0){
					type = NOENCONTRADO;		//	Como se trata el caso excepcional de la URL que no apunta a ningún fichero html
				}
				else{
					type = OK;					//	En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera correspondiente, y el envio del fichero se hace en blockes de un máximo de  8kB
				}	
			create_errorMessage(type,descriptorFichero,exten,df);
		}
		close(df);
	}
	close(descriptorFichero);
	exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros
	
	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verficiar que los argumentos que se pasan al iniciar el programa son los esperados
	//

	if(argc != 3) {
		(void)printf("USO: %s [port] [server_directory]\n",argv[0]);
		exit(2);
	}
	//
	//  Verficiar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//
	struct stat dir;
	if ( stat(argv[2],&dir) < 0) {
		(void)printf("ERROR: No existe el fichero/directorio %s\n",argv[2]); 
	    exit(5);
	}

	if ( access(argv[2], W_OK) != 0) {
		(void)printf("ERROR: No se dispone de permiso de escritura para el directorio %s\n", argv[2]);
		exit(6);
	}


	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues
	
	debug(LOG,"web server starting...", argv[1] ,getpid());
	
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);
	
	port = atoi(argv[1]);
	
	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);
	
	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/
	
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);
	
	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);
	
	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { 	// Proceso padre
				(void)close(socketfd);
			}
		}
	}
}
