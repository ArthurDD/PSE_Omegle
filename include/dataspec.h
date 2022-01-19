#ifndef DATASPEC_INCLUDE_H
#define DATASPEC_INCLUDE_H

/* module datathread : donnees specifiques */

/* donnees specifiques */
typedef struct DataSpec_t {
  pthread_t id;               /* identifiant du thread */
  int libre;                  /* indicateur de terminaison */
/* ajouter donnees specifiques après cette ligne */
  int tid;                    /* identifiant logique */
  int canal;                  /* canal de communication */
  int tid_dest;	       /* ID du destinataire */
  int last_tid_dest;		/*ID du destinataire précédent */
  sem_t sem;                  /* semaphore de reveil */
  char pseudo[LIGNE_MAX];     /* pseudo de la session */
  int tid_request;		/*determine si le client a une demande de connexion*/
  bool pret;
} DataSpec;

#endif
