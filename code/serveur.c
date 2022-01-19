#include "pse.h"


#define     CMD                 "serveur"
#define     NB_WORKERS     50

void creerCohorteWorkers(void);
int chercherWorkerLibre(void);
void *threadWorker(void *arg);
bool sessionClient(DataSpec *DataTh);
int ecrireJournal(char *buffer);
void get_pseudo(int canal, char pseudo[LIGNE_MAX]);
void verif_lgLue(int lgLue);
int rand_a_b(int a, int b);
void changement_partenaire(DataSpec *dataTh);
int chercher_pseudo(DataSpec *DataTh);
void changement_partenaire_volontaire(DataSpec *dataTh);

void remiseAZeroJournal(void);
void lockMutexJournal(void);
void unlockMutexJournal(void);
void lockMutexCanal(int numWorker);
void unlockMutexCanal(int numWorker);
void lockMutexPseudo(void);
void unlockMutexPseudo(void);
void lockMutexNbCo(void);
void unlockMutexNbCo(void);
void lockMutexLastTidDest(void);
void unlockMutexLastTidDest(void);
void lockMutexTidDest(void);
void unlockMutexTidDest(void);
void lockMutexTidRequest(void);
void unlockMutexTidRequest(void);
void lockMutexBoucle(void);
void unlockMutexBoucle(void);



int fdJournal;
DataSpec dataSpec[NB_WORKERS];
sem_t semWorkersLibres;
int nbCo;

sem_t semPseudos;


// la remise a zero du journal modifie le descripteur du fichier, il faut donc
// proteger par un mutex l'ecriture dans le journal et la remise a zero
pthread_mutex_t mutexJournal;

// pour l'acces au canal d'un worker peuvant etre en concurrence la recherche
// d'un worker libre et la mise à -1 du canal par le worker
pthread_mutex_t mutexCanal[NB_WORKERS];

//On va modifier le pseudo du thread à chaque fois qu'un thread est initialisé.
//On protège donc cette modification grace à un mutex
pthread_mutex_t mutexPseudo;

//On store dans une variable globale le nombre de clients connectés
pthread_mutex_t mutexNbCo;

//On modifie le destinataire et le destinataire précédent d'un client.
pthread_mutex_t mutexLastTidDest;
pthread_mutex_t mutexTidDest;

//On modifie le booléen déterminant si le client a bien reçu une demande de connexion
pthread_mutex_t mutexTidRequest;

//On ne veut pas que deux clients se connectent en même temps à un autre
pthread_mutex_t mutexBoucle;

int main(int argc, char *argv[]) {
    short port;
    int ecoute, canal, ret;
    struct sockaddr_in adrEcoute, adrClient;
    unsigned int lgAdrClient;
    int numWorkerLibre;

    srand(time(NULL)); // initialisation de rand
    lockMutexNbCo();
    nbCo = 0;
    unlockMutexNbCo();

    fdJournal = open("journal.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (fdJournal == -1)
        erreur_IO("ouverture journal");

    creerCohorteWorkers();

    ret = sem_init(&semPseudos,0,0);
    if (ret == -1)
        erreur_IO("init sem workers libres");

    ret = sem_init(&semWorkersLibres, 0, NB_WORKERS);
    if (ret == -1)
        erreur_IO("init sem workers libres");

    if (argc != 2)
        erreur("usage: %s port\n", argv[0]);

    port = (short)atoi(argv[1]);

    printf("%s: creating a socket\n", CMD);
    ecoute = socket (AF_INET, SOCK_STREAM, 0);
    if (ecoute < 0)
        erreur_IO("socket");
    
    adrEcoute.sin_family = AF_INET;
    adrEcoute.sin_addr.s_addr = INADDR_ANY;
    adrEcoute.sin_port = htons(port);
    printf("%s: binding to INADDR_ANY address on port %d\n", CMD, port);
    ret = bind (ecoute,    (struct sockaddr *)&adrEcoute, sizeof(adrEcoute));
    if (ret < 0)
        erreur_IO("bind");
    
    printf("%s: listening to socket\n", CMD);
    ret = listen (ecoute, 5);
    if (ret < 0)
        erreur_IO("listen");
    
    while (VRAI) {
        printf("%s: accepting a connection\n", CMD);
        canal = accept(ecoute, (struct sockaddr *)&adrClient, &lgAdrClient);
        if (canal < 0)
            erreur_IO("accept");

        printf("%s: adr %s, port %hu\n", CMD,
	            stringIP(ntohl(adrClient.sin_addr.s_addr)), ntohs(adrClient.sin_port));

        ret = sem_wait(&semWorkersLibres);    // attente d'un worker libre
        if (ret == -1)
            erreur_IO("wait sem workers libres");
        numWorkerLibre = chercherWorkerLibre();

        dataSpec[numWorkerLibre].canal = canal;
        sem_post(&dataSpec[numWorkerLibre].sem);    // reveil du worker
        if (ret == -1)
            erreur_IO("post sem worker");
    }

    if (close(ecoute) == -1)
        erreur_IO("fermeture ecoute");

    if (close(fdJournal) == -1)
        erreur_IO("fermeture journal");

    exit(EXIT_SUCCESS);
}

void creerCohorteWorkers(void) {
    int i, ret;

    for (i= 0; i < NB_WORKERS; i++) {
        dataSpec[i].canal = -1;
        dataSpec[i].tid_dest = -1;
        dataSpec[i].tid = i;
        dataSpec[i].last_tid_dest = -1;
        dataSpec[i].tid_request = -1;

        ret = sem_init(&dataSpec[i].sem, 0, 0);
        if (ret == -1)
            erreur_IO("init sem worker");

        ret = pthread_create(&dataSpec[i].id, NULL, threadWorker, &dataSpec[i]);
        if (ret != 0)
            erreur_IO("creation thread");
    }
}

// retourne le no. du worker libre trouve ou -1 si pas de worker libre
int chercherWorkerLibre(void) {
    int numWorkerLibre = -1, i = 0, canal;

    while (numWorkerLibre < 0 && i < NB_WORKERS) {
        lockMutexCanal(i);
        canal = dataSpec[i].canal;
        unlockMutexCanal(i);

        if (canal == -1)
            numWorkerLibre = i;
        else
            i++;
    }

    return numWorkerLibre;
}

void *threadWorker(void *arg) {
    DataSpec *dataTh = (DataSpec *)arg;
    int ret;
    char ligne_renv[LIGNE_MAX];
    int dest_id;
    int lgLue;
    char ligne[LIGNE_MAX];

    while (VRAI) {
        ret = sem_wait(&dataTh->sem); // attente reveil par le thread principal
        if (ret == -1)
            erreur_IO("wait sem worker");

        printf("%s: worker %d reveil\n", CMD, dataTh->tid);
        lockMutexNbCo();
        nbCo ++;    //Un client s'est connecté, on incrémente donc nbCo
        unlockMutexNbCo();
        strcpy(ligne_renv,"Bonjour et bienvenue sur le serveur !");
        ecrireLigne(dataTh->canal, ligne_renv);
    
        char pseudo[LIGNE_MAX];
        get_pseudo(dataTh->canal, pseudo);  //On demande le pseudo du client
        //sem_wait(&semPseudos);

        lockMutexPseudo();
        strcpy(dataTh->pseudo, pseudo); 
        unlockMutexPseudo();

        bool fin = false;
        while(!fin)
        {
            strcpy(ligne_renv, "En attente d'une personne disponible...");
            ecrireLigne(dataTh->canal, ligne_renv);

            printf("Le last_tid_dest de %s est : %d\n", dataTh->pseudo, dataSpec[dataTh->tid].last_tid_dest);
            printf("Le tid_dest de %s est : %d\n", dataTh->pseudo, dataTh->tid_dest);
            bool connected = false;
            dataTh->pret = true;
            while (!connected)  
            {
                dest_id = rand_a_b(0, NB_WORKERS);

                if (dataTh->tid_request != -1)  //SI quelqu'un essaie de se connecter à cette personne, on écoute sa réponse
                {
                    printf("ON A UNE DEMANDE POUR %s!!!\n", dataTh->pseudo);
                    lgLue = lireLigne(dataTh->canal, ligne);
                    verif_lgLue(lgLue);

                    if (strcmp(ligne, "ACCEPT!") ==0)
                    {
                        lockMutexTidDest();
                        dataTh->tid_dest = dataTh->tid_request; //Je change de destinataire
                        unlockMutexTidDest();

                        lockMutexTidDest();
                        dataSpec[dataSpec[dataTh->tid_dest].tid_dest].tid_dest = -1; //Le destinataire de mon destinataire change est reset
                        dataSpec[dataTh->tid_dest].tid_dest = dataTh->tid;   //Mon destinataire change de destinataire (=moi)
                        unlockMutexTidDest();

                        lockMutexTidRequest();
                        dataTh->tid_request = -1;           //Je reset la requête aux deux
                        dataSpec[dataTh->tid_dest].tid_request = -1;
                        unlockMutexTidRequest();

                        strcpy(ligne_renv,"Vous êtes maintenant en ligne avec ");   //On prévient le client qu'il est en ligne
                        strcat(ligne_renv, dataSpec[dataTh->tid_dest].pseudo);
                        strcat(ligne_renv, ".");
                        ecrireLigne(dataTh->canal, ligne_renv);

                        strcpy(ligne_renv,"Demande de connexion acceptée ! Vous êtes maintenant en ligne avec ");   //On prévient son destinataire 
                        strcat(ligne_renv, dataTh->pseudo);
                        strcat(ligne_renv, ".");
                        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);

                        dataTh->pret = false;
                        dataSpec[dest_id].pret = false;
                        connected = true;
                    }

                    else if (strcmp(ligne, "REJECT!") == 0)
                    {
                        strcpy(ligne_renv, "Son pseudo ne me disait rien de bon.");
                        ecrireLigne(dataTh->canal, ligne_renv);

                        strcpy(ligne_renv, "Désolé, mais cette personne ne veut pas de toi.");
                        ecrireLigne(dataSpec[dataTh->tid_request].canal, ligne_renv);
                        strcpy(ligne_renv, "Désolé, mais cette personne ne veut pas de toi. (Je le dis deux fois pour être sûr que tu aies compris)");
                        ecrireLigne(dataSpec[dataTh->tid_request].canal, ligne_renv);
                        strcpy(ligne_renv, "");
                        ecrireLigne(dataSpec[dataTh->tid_request].canal, ligne_renv);
                    }
                }

                lockMutexBoucle();
                if (dest_id != dataTh->tid && dataSpec[dest_id].tid_dest == -1 && 
                    dataSpec[dest_id].canal != -1 && dest_id != dataTh->last_tid_dest && 
                    dataSpec[dest_id].pret == true) 
                    // Si le dest_id n'est pas le canal lui même, si ce n'est pas un client déjà en communication 
                    // et si le canal dest est en ligne et si ce n'est pas le canal avec lequel on était à l'instant en communication
                {
                    printf("ON EST LAA POUR %s\n", dataTh->pseudo);
                    printf("tid trouvé : %d\n", dest_id);
                    dataTh->pret = false;
                    lockMutexTidDest();
                    dataTh->tid_dest = dest_id;
                    unlockMutexTidDest();

                    lockMutexCanal(dest_id);
                    dataSpec[dest_id].tid_dest = dataTh->tid;
                    unlockMutexCanal(dest_id);

                    strcpy(ligne_renv, "Personne trouvée ! Mise en relation...");
                    ecrireLigne(dataTh->canal, ligne_renv);

                    connected = true;
                } 

                else if (dataTh->tid_dest != -1 && dataTh->pret == true) //S'il a déjà été affecté par son destinataire 
                {
                    printf("ON EST ICIII POUR %s\n", dataTh->pseudo);
                    connected = true;
    		        dataTh->pret = false;
                    dataSpec[dataTh->tid_dest].pret = false;
                    strcpy(ligne_renv,"");
                    ecrireLigne(dataTh->canal, ligne_renv);
                    strcpy(ligne_renv,"Vous êtes maintenant en ligne avec ");
                    strcat(ligne_renv, dataSpec[dataTh->tid_dest].pseudo);
                    ecrireLigne(dataTh->canal, ligne_renv);
                    
                    strcpy(ligne_renv,"");
                    ecrireLigne(dataTh->canal, ligne_renv);
                    strcpy(ligne_renv,"Pour changer d'interlocuteur, écrivez NEXT!\n");
                    ecrireLigne(dataTh->canal, ligne_renv);
                    strcpy(ligne_renv,"Pour mettre fin à votre session, écrivez FIN!\n");     
                    ecrireLigne(dataTh->canal, ligne_renv);

                    strcpy(ligne_renv,"");
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);                    
                    strcpy(ligne_renv,"Vous êtes maintenant en ligne avec ");
                    strcat(ligne_renv, dataTh->pseudo);
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
                    
                    strcpy(ligne_renv,"");
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv); 
                    strcpy(ligne_renv,"Pour changer d'interlocuteur, écrivez NEXT!\n");   
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
                    strcpy(ligne_renv,"Pour mettre fin à votre session, écrivez FIN!\n");
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
                    strcpy(ligne_renv,"");
                    ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv); 
                }
                unlockMutexBoucle();
            }

            fin = sessionClient(dataTh);
            if (fin)
                printf("Bon bah %s a ragequit...\n", dataTh->pseudo);
            
            else
                printf("Bon bah %s en avait marre de son interlocuteur...\n", dataTh->pseudo);
        }

        lockMutexCanal(dataTh->tid);
        dataTh->canal = -1;
        unlockMutexCanal(dataTh->tid);

        ret = sem_post(&semWorkersLibres);    // incrementer le nb de workers libres
        if (ret == -1)
            erreur_IO("post sem workers libres");

        printf("%s: worker %d sommeil\n", CMD, dataTh->tid);
    }

    pthread_exit(NULL);
}


bool sessionClient(DataSpec *dataTh) {
    bool fin_chat = false;
    bool fin_session = false ;
    char ligne[LIGNE_MAX];
    int lgLue;
    char ligne_renv[LIGNE_MAX];
    int canal = dataTh->canal;

    /*pid_t verif_rejeton;
    verif_rejeton = fork();

    if (verif_rejeton == 0)
    {
        while
    }*/

    int canal_dest = dataSpec[dataTh->tid_dest].canal;
    char *pseudo_dest = dataSpec[dataTh->tid_dest].pseudo;
    while (!fin_chat && (dataTh->tid_dest != -1)) {
        lgLue = lireLigne(canal, ligne);
        verif_lgLue(lgLue);

        printf("%s: reception %d octets : \"%s\"\n", CMD, lgLue, ligne);

        if (dataTh->tid_dest == -1)
        {
            strcpy(ligne_renv, "Vous voilà retourné à l'accueil.");
            ecrireLigne(dataTh->canal, ligne_renv);
            fin_chat = true;
        }
        else if (strcmp(ligne, "FIN!") == 0) {
                printf("%s: fin client\n", CMD);
                changement_partenaire(dataTh);
                fin_chat = true;
                fin_session = true;
         }
        else if (strcmp(ligne, "NEXT!") == 0) {
                printf("%s: changement de partenaire\n", CMD);
                changement_partenaire(dataTh);
                fin_chat = true;
        }
        else if (strcmp(ligne, "init") == 0) {
            printf("%s: remise a zero journal\n", CMD);
            remiseAZeroJournal();
        }

        else if (strcmp(ligne, "HELP!") == 0)
        {
            strcpy(ligne_renv,"");
            ecrireLigne(dataTh->canal, ligne_renv); 
            strcpy(ligne_renv,"Pour changer d'interlocuteur, écrivez NEXT!");   
            ecrireLigne(dataTh->canal, ligne_renv);
            strcpy(ligne_renv,"Pour mettre fin à votre session, écrivez FIN!");
            ecrireLigne(dataTh->canal, ligne_renv);
            strcpy(ligne_renv,"Pour revoir cette liste de commandes, écrivez HELP!");
            ecrireLigne(dataTh->canal, ligne_renv);
        }

        else if (strcmp(ligne, "CHERCHER!") == 0)
        {
            int tid_dest = chercher_pseudo(dataTh);

            if (tid_dest != -1) //Si le client est bel et bien connecté
            {
                char ligne_renv[LIGNE_MAX];
                strcpy(ligne_renv,"Il semblerait que ");
                strcat(ligne_renv, dataTh->pseudo);
                strcat(ligne_renv, " veuille communiquer avec vous. Acceptez-vous ? (ACCEPT!/REJECT!)");
                ecrireLigne(dataSpec[dataTh->tid_request].canal, ligne_renv);
            }
        }

        else if (strcmp(ligne, "ACCEPT!") == 0) 
        {
            printf("tid_request du receveur %s : %d\n", dataTh->pseudo, dataTh->tid_request);
            if (dataTh->tid_request != -1)    //Si on lui a bel et bien demandé de communiquer
            {
                printf("Demande de connexion acceptée par %s\n", dataTh->pseudo);
                changement_partenaire_volontaire(dataTh);
            }
        }

        else if (strcmp(ligne, "REJECT!") == 0)
        {
            strcpy(ligne_renv,"Sage décision, je n'avais pas non plus envie de lui parler de toute façon.");
            ecrireLigne(dataTh->canal, ligne_renv);
        }

        else if (strcmp(ligne, "ACCUEIL!") == 0)
        {
            strcpy(ligne_renv, "Vous voilà retourné à l'accueil.");
            ecrireLigne(dataTh->canal, ligne_renv);
            fin_chat = true;
        }
        else if (ecrireJournal(ligne) != -1) {
                printf("%s: ligne de %d octets ecrite dans journal\n", CMD, lgLue);

                strcpy(ligne_renv, "");
                strcat(ligne_renv, "Message de ");
                strcat(ligne_renv, dataTh->pseudo);
                strcat(ligne_renv, " : ");
                strcat(ligne_renv, ligne);
                printf("La ligne qui va être envoyée à %s est la suivante : \n\"%s\"\n",pseudo_dest, ligne_renv);

                ecrireLigne(canal_dest, ligne_renv);
        }

        else
            erreur_IO("ecriture journal");

        

    } // fin while

    printf("Sortie de la fonction session\n");
    if (fin_session)
    {
        if (close(canal) == -1)
        erreur_IO("fermeture canal");
    }
    return(fin_session);
}



void get_pseudo(int canal, char pseudo[LIGNE_MAX])
{
    bool fin = false;
    char ligne_renv[LIGNE_MAX];

    strcpy(ligne_renv, "Rentrez votre pseudo SVP : ");
    int lgLue  = ecrireLigne(canal, ligne_renv);

    while (!fin)
    {
        lgLue = lireLigne(canal, pseudo);
        verif_lgLue(lgLue);

        fin = true;
        for (int i=0 ; i<NB_WORKERS ; i++)
        {
            if (strncmp(pseudo, dataSpec[i].pseudo, LIGNE_MAX) == 0)
            {
                strcpy(ligne_renv, "Erreur, pseudo déjà utilisé. Veuillez en choisir un autre : ");
                ecrireLigne(canal, ligne_renv);
                fin = false;
            }
        }
    }

    strcpy(ligne_renv, "Bien ouej, le pseudo n'est pas pris!");
    lgLue = ecrireLigne(canal, ligne_renv);
    //sem_post(&semPseudos);
    //return(pseudo);
}



int rand_a_b(int a, int b)
{
    return rand()%(b-a) +a;
}

void verif_lgLue(int lgLue)
{
    if (lgLue < 0)
            erreur_IO("lireLigne");
        else if (lgLue == 0)
            erreur("interruption client\n");
}



void changement_partenaire(DataSpec *dataTh)
{
    printf("%s est dans la fonction changement_partenaire.\n", dataTh->pseudo);
    if (dataTh->tid_dest != -1) //Si le tid_dest n'a pas déjà été reset
    {
        char ligne_renv[LIGNE_MAX];
        
        printf("%s est devenu un REJETON.\n", dataSpec[dataTh->tid_dest].pseudo);

        strcpy(ligne_renv,"");
        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
        strcpy(ligne_renv, "Votre interlocuteur a mis fin a la discussion.");
        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);

        strcpy(ligne_renv,"Pour continuer --> écrivez ACCUEIL!");   
        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
        strcpy(ligne_renv,"Pour quitter --> écrivez FIN!");
        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);

        lockMutexLastTidDest();
        dataSpec[dataTh->tid_dest].last_tid_dest = dataSpec[dataTh->tid_dest].tid_dest;
        unlockMutexLastTidDest();

        lockMutexLastTidDest();
        dataSpec[dataTh->tid].last_tid_dest = dataTh->tid_dest;
        unlockMutexLastTidDest();
        
        lockMutexTidDest();
        printf("Je reset la valeur tid_dest de %s\n", dataSpec[dataTh->tid_dest].pseudo);
        dataSpec[dataTh->tid_dest].tid_dest = -1;
        unlockMutexTidDest();

        lockMutexTidDest();
        //printf("Je suis dans la f changement_partenaire\n");
        dataSpec[dataTh->tid].tid_dest = -1;
        unlockMutexTidDest();

    }

}


int chercher_pseudo(DataSpec *dataTh)    /*Fonction renvoyant le tid de la personne avec laquelle le client actuel veut communiquer
                                          Si pseudo non trouvé ou si le client se rétracte, la fonction renvoie -1*/
{
    int lgLue;
    char pseudo[LIGNE_MAX];
    char ligne_renv[LIGNE_MAX];

    strcpy(ligne_renv,"");
    ecrireLigne(dataTh->canal, ligne_renv); 
    strcpy(ligne_renv,"Entrez le pseudo de la personne à rechercher :");   
    ecrireLigne(dataTh->canal, ligne_renv);
    
    lgLue = lireLigne(dataTh->canal, pseudo);
    verif_lgLue(lgLue);
    
    strcpy(ligne_renv, "Compris, nous allons chercher ");
    strcat(ligne_renv, pseudo);
    strcat(ligne_renv, " !");
    ecrireLigne(dataTh->canal, ligne_renv);
    strcpy(ligne_renv,"");
    ecrireLigne(dataTh->canal, ligne_renv);

    int dest_tid = -1;

    for (int i = 0; i <NB_WORKERS ; i++)
    {
        if (strcmp(dataSpec[i].pseudo, pseudo) == 0)
        {
            dest_tid = dataSpec[i].tid;
            printf("Pseudo trouvé !\n");
        } 
    }

    if (dest_tid != -1)
    {   
        char rep[LIGNE_MAX];
        strcpy(ligne_renv,"Pseudo trouvé ! Souhaitez-vous rentrer en contact avec cette personne ? (OUI!/NON!)");
        ecrireLigne(dataTh->canal, ligne_renv); 

        lgLue = lireLigne(dataTh->canal, rep);
        verif_lgLue(lgLue);

        strcpy(ligne_renv, rep);
        ecrireLigne(dataTh->canal, ligne_renv);
        
        if (strcmp(rep, "OUI!") == 0)
        {
            lockMutexTidRequest();
            dataSpec[dest_tid].tid_request = dataTh->tid;   //On signale a dest_id qu'il a bien reçu une demande de connexion
            unlockMutexTidRequest();

            lockMutexTidRequest();
            dataTh->tid_request = dest_tid;   //On rentre également l'id de la personne cherchée pour pouvoir lui envoyer la demande de connexion
            unlockMutexTidRequest();
            return(dest_tid);
        }

        else if (strcmp(rep, "NON!") == 0)
        {
            strcpy(ligne_renv,"Sage décision, je n'avais pas non plus envie de lui parler de toute façon.");
            ecrireLigne(dataTh->canal, ligne_renv);
            return(-1);
        }

        else
        {
            strcpy(ligne_renv,"Oula j'ai pô capté. RETRAAAAITE !");
            ecrireLigne(dataTh->canal, ligne_renv);
            return(-1);
        }
    }
    else
    {
        strcpy(ligne_renv,"Coup dur pour le jeune français, aucun ");
        strcat(ligne_renv, pseudo);
        strcat(ligne_renv, " n'a été trouvé...");
        ecrireLigne(dataTh->canal, ligne_renv);
        return(-1);
    }
}


void changement_partenaire_volontaire(DataSpec *dataTh)
{
        char ligne_renv[LIGNE_MAX];

        printf("%s est passé dans changement volontaire\n", dataTh->pseudo);

        strcpy(ligne_renv, "Vous avez été abandonné. Entrez n'importe quel message pour retourner à l'accueil.");
        ecrireLigne(dataSpec[dataTh->tid_dest].canal, ligne_renv);
        lockMutexTidDest();
        dataSpec[dataTh->tid_dest].tid_dest = -1; //Notre destinataire actuel sort de session client
        unlockMutexTidDest();

        strcpy(ligne_renv, "Vous avez été abandonné. Entrez n'importe quel message pour retourner à l'accueil.");
        ecrireLigne(dataSpec[dataSpec[dataTh->tid_request].tid_dest].canal, ligne_renv);
        lockMutexTidDest();
        dataSpec[dataSpec[dataTh->tid_request].tid_dest].tid_dest = -1; //Le destinataire de notre futur destinataire sort de session
        unlockMutexTidDest(); 

        lockMutexTidDest();
        dataTh->tid_dest = dataTh->tid_request; //Je change de destinataire
        unlockMutexTidDest();

        lockMutexTidDest();
        dataSpec[dataTh->tid_request].tid_dest = dataTh->tid;   //Mon destinataire change de destinataire (=moi)
        unlockMutexTidDest();

        lockMutexTidRequest();
        dataTh->tid_request = -1;           //Je reset la requête aux deux
        dataSpec[dataTh->tid_dest].tid_request = -1;
        unlockMutexTidRequest();
}



int ecrireJournal(char *buffer)
{
    int lgLue;

    lockMutexJournal();
    lgLue = ecrireLigne(fdJournal, buffer);
    unlockMutexJournal();
    return lgLue;
}

// le fichier est ferme et rouvert vide
void remiseAZeroJournal(void) {
    lockMutexJournal();

    if (close(fdJournal) == -1)
        erreur_IO("raz journal - fermeture");

    fdJournal = open("journal.log", O_WRONLY|O_TRUNC|O_APPEND, 0644);
    if (fdJournal == -1)
        erreur_IO("raz journal - ouverture");

    unlockMutexJournal();
}

void lockMutexJournal(void)
{
    int ret;

    ret = pthread_mutex_lock(&mutexJournal);
    if (ret != 0)
        erreur_IO("lock mutex journal");
}

void unlockMutexJournal(void)
{
    int ret;

    ret = pthread_mutex_unlock(&mutexJournal);
    if (ret != 0)
        erreur_IO("lock mutex journal");
}

void lockMutexCanal(int numWorker)
{
    int ret;

    ret = pthread_mutex_lock(&mutexCanal[numWorker]);
    if (ret != 0)
        erreur_IO("lock mutex dataspec");
}

void unlockMutexCanal(int numWorker)
{
    int ret;

    ret = pthread_mutex_unlock(&mutexCanal[numWorker]);
    if (ret != 0)
        erreur_IO("lock mutex dataspec");
}

void lockMutexPseudo()
{
    int ret;

    ret = pthread_mutex_lock(&mutexPseudo);
    if (ret!= 0)
        erreur_IO("lock mutex pseudo");
}

void unlockMutexPseudo()
{
    int ret;

    ret = pthread_mutex_unlock(&mutexPseudo);
    if (ret != 0)
        erreur_IO("unlock mutex pseudo");
}


void lockMutexNbCo()
{
    int ret;

    ret = pthread_mutex_lock(&mutexNbCo);
    if (ret!= 0)
        erreur_IO("lock mutex NbCo");
}

void unlockMutexNbCo()
{
    int ret;

    ret = pthread_mutex_unlock(&mutexNbCo);
    if (ret != 0)
        erreur_IO("unlock mutex NbCo");
}

void lockMutexLastTidDest()
{
    int ret;

    ret = pthread_mutex_lock(&mutexLastTidDest);
    if (ret!= 0)
        erreur_IO("lock mutex LastTidDest");
}

void unlockMutexLastTidDest()
{
    int ret;

    ret = pthread_mutex_unlock(&mutexLastTidDest);
    if (ret != 0)
        erreur_IO("unlock mutex LastTidDest");
}


void lockMutexTidDest()
{
    int ret;

    ret = pthread_mutex_lock(&mutexTidDest);
    if (ret!= 0)
        erreur_IO("lock mutex TidDest");
}

void unlockMutexTidDest()
{
    int ret;

    ret = pthread_mutex_unlock(&mutexTidDest);
    if (ret != 0)
        erreur_IO("unlock mutex TidDest");
}


void lockMutexTidRequest()
{
    int ret;

    ret = pthread_mutex_lock(&mutexTidRequest);
    if (ret!= 0)
        erreur_IO("lock mutex TidDest");
}

void unlockMutexTidRequest()
{
    int ret;

    ret = pthread_mutex_unlock(&mutexTidRequest);
    if (ret != 0)
        erreur_IO("unlock mutex TidDest");
}


void lockMutexBoucle(void)
{
    int ret;

    ret = pthread_mutex_lock(&mutexBoucle);
    if (ret != 0)
        erreur_IO("unlock mutex Boucle");
}


void unlockMutexBoucle(void)
{
    int ret;

    ret = pthread_mutex_unlock(&mutexBoucle);
    if (ret != 0)
        erreur_IO("unlock mutex Boucle");
}
