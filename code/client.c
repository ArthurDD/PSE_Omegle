#include "pse.h"

#define CMD     "client"


void *lecture(void *arg);

int main(int argc, char *argv[]) {
    int sock, ret;
    struct sockaddr_in *adrServ;
    int fin = FAUX;
    char ligne_ecr[LIGNE_MAX];
    
    int lgEcr;

    if (argc != 3)
        erreur("usage: %s machine port\n", argv[0]);

    printf("%s: creating a socket\n", CMD);
    sock = socket (AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        erreur_IO("socket");

    printf("%s: DNS resolving for %s, port %s\n", CMD, argv[1], argv[2]);
    adrServ = resolv(argv[1], argv[2]);
    if (adrServ == NULL)
        erreur("adresse %s port %s inconnus\n", argv[1], argv[2]);

    printf("%s: adr %s, port %hu\n", CMD,
	                stringIP(ntohl(adrServ->sin_addr.s_addr)),
	                ntohs(adrServ->sin_port));

    printf("%s: connecting the socket\n", CMD);
    ret = connect(sock, (struct sockaddr *)adrServ, sizeof(struct sockaddr_in));
    if (ret < 0)
        erreur_IO("connect");

    DataSpec lecteur;
    ret = pthread_create(&lecteur.id, NULL, lecture, &sock);
    if (ret != 0)
        erreur_IO("creation thread");
    
    while (!fin) {
        printf("> ");	
        if (fgets(ligne_ecr, LIGNE_MAX, stdin) == NULL)
            erreur("saisie fin de fichier\n");

        lgEcr = ecrireLigne(sock, ligne_ecr);
        if (lgEcr == -1)
            erreur_IO("ecrire ligne");

        //printf("%s: %d bytes sent\n", CMD, lgEcr);

        if (strcmp(ligne_ecr, "fin\n") == 0)
            fin = VRAI;
    }

    if (close(sock) == -1)
        erreur_IO("close socket");

    exit(EXIT_SUCCESS);
}


void *lecture(void *arg)
{
    int *sock_ = (int *)arg;
    int sock = *sock_;
    char ligne[LIGNE_MAX];
    int lgEcr;

    while (VRAI) {
            lgEcr = lireLigne(sock, ligne);
            if (lgEcr == -1)
                erreur_IO("lire ligne");

            else if (lgEcr == 0)
                erreur_IO("Fin de la session.");

            printf("%s\n\n> ", ligne);
        }
}