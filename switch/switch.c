//
// Created by cpasjuste on 26/03/18.
//

#include <unistd.h>

unsigned int sleep(unsigned int nb_sec) {

    usleep(nb_sec * 1000 * 1000);
}
