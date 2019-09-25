#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

struct bg_process {
	int pid;
	int status;
};

volatile int foreground_counter = 0;
volatile int background_counter = 0;
volatile int foreground_pids [MAX_LINE_LENGTH/2 + 5];
volatile struct bg_process background [32];

static sigset_t mask_sigchld;
static sigset_t mask_sigsuspend;


int my_write (int fd, char* str, int len) { //wypisz wiadomosc na dany deskryptor

	int w = 0;
	while (true) {
		int cur_w = write(fd, str, len - w);
        if (cur_w != -1) {
            w += cur_w;
        } 
		if (w == len || errno != EINTR) {
			break;
		}
	}
	return w;
}


static void sigchild_handler(int signo) { //handler dla SIGCHLD
    int errno_copy = errno;
	while (true) {
		int child_stat;
		int child_pid = waitpid(-1, &child_stat, WNOHANG);
		if (child_pid > 0) {
			bool from_foreground = false;
			for (int i = 0; i < foreground_counter; i++) {
				if (foreground_pids[i] == child_pid) {
					foreground_pids[i] = foreground_pids[foreground_counter - 1];
					foreground_pids[foreground_counter - 1] = 0;
					from_foreground = true;
					break;
				}
			}
			if (from_foreground) {
				foreground_counter--;
			}
			else {
				background_counter--;
				for (int i = 0; i < 32; i++) {
					if (background[i].pid == 0) {
						background[i].pid = child_pid;
						background[i].status = child_stat;
						break;
					}
				}
			}
		}
		else {
			break;
		}
	}
    errno = errno_copy;
}

void change_signals_handling() { //zmień handler w SIGCHLD oraz ingoruj SIGINT
    struct sigaction sigact;
    sigact.sa_handler = sigchild_handler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    if (sigaction(SIGCHLD, &sigact, NULL) == -1) {
        exit(EXEC_FAILURE);
    }
    sigact.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &sigact, NULL) == -1) {
        exit(EXEC_FAILURE);
    }
}

void lock_sigchld() { //zablokuj SIGCHLD
    if (sigprocmask(SIG_BLOCK, &mask_sigchld, NULL) == -1) {
        exit(EXEC_FAILURE);
    }
}

void unlock_sigchld() { //odblokuj SIGCHLD
    if (sigprocmask(SIG_UNBLOCK, &mask_sigchld, NULL) == -1) {
        exit(EXEC_FAILURE);
    }
}

void print_error(char* arg, char* message) { //wypisz wiadomość na stderr
    if (arg == NULL) {
        char str[strlen(message) + 5];
        sprintf(str, "%s\n", message);
    	my_write(2, str, strlen(str));
        return; 
    }

    char str[strlen(arg) + strlen(message) + 5];
    sprintf(str, "%s%s\n", arg, message);
    int w = 0;
    my_write(2, str, strlen(str));
}

builtin_pair* get_shell_command(char* command) { //znajdź komende shella
    builtin_pair* element_of_table = builtins_table;
    while(element_of_table->name != NULL) {
        if (strcmp(element_of_table->name,command) == 0) {
            return element_of_table;
        }    
        element_of_table++;
    }
    return NULL;
}

void execute(char** args) { //wykonaj daną komendę
    if(execvp(args[0], args)==-1) {
        if (errno == EACCES) {
            print_error(args[0], ": permission denied");
        }
        else if (errno == ENOENT) {
            print_error(args[0], ": no such file or directory");
        }
        else {
            print_error(args[0], ": exec error");
        }
        exit(EXEC_FAILURE);
    }
    exit(0);
}

void set_IO(redir** redirs) { //ustaw deskryptor 0 i 1 dla programu
    if (redirs[0] != NULL) {
        if (close(0) == -1) {
            exit(EXEC_FAILURE);
        }
        if (open(redirs[0]->filename, O_RDONLY) == -1) {
            if (errno == EACCES) {
                print_error(redirs[0]->filename, ": permission denied");
            }
            else if (errno == ENOENT) {
                print_error(redirs[0]->filename, ": no such file or directory");
            }
            exit(EXEC_FAILURE);
        }
    }
    if (redirs[1] != NULL) {
        if (close(1) == -1) {
            exit(EXEC_FAILURE);
        }
        if (IS_ROUT(redirs[1]->flags)) {
            if (open(redirs[1]->filename, O_CREAT | O_WRONLY | O_TRUNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == -1) {
                if (errno == EACCES) {
                    print_error(redirs[1]->filename, ": permission denied");
                }
                exit(EXEC_FAILURE);
            }
            
        }
        else if (IS_RAPPEND(redirs[1]->flags)) {
            if (open(redirs[1]->filename, O_CREAT | O_WRONLY | O_APPEND,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == -1) {
                if (errno == EACCES) {
                    print_error(redirs[1]->filename, ": permission denied");
                }
                exit(EXEC_FAILURE);
            }
        }
    }
}


//stwórz dziecko
int make_child(char** args, redir** redirs, int current, int amount_of_all, int current_pipe, bool is_background) {
    int fildes[2];
    fildes[0] = 0;
    fildes[1] = 1;
    if (current < amount_of_all) {
        if (pipe(fildes) == -1) {
            return -1; 
        }
    }
    pid_t pr = fork();
    if(pr==0) {
    	struct sigaction sigact;
		sigact.sa_handler = SIG_DFL;
		sigemptyset(&sigact.sa_mask);
	    sigact.sa_flags = SA_RESTART;
	    
        if ( sigaction(SIGCHLD, &sigact, NULL) == -1) {
	    	exit(EXEC_FAILURE);
	    }
	    if ( sigaction(SIGINT, &sigact, NULL) == -1) {
	    	exit(EXEC_FAILURE);
	    }

	    unlock_sigchld();

    	if (is_background) {
    		if (setsid() == -1) {
                exit(EXEC_FAILURE);
            }
    	}
        if (fildes[0] != 0 ) {
            if (close(fildes[0]) == -1) {
                exit(EXEC_FAILURE);
            }
        }
        if (fildes[1] != 1) {
            if (close(1) == -1) {
                exit(EXEC_FAILURE);
            }
            if (dup(fildes[1]) == -1) {
                exit(EXEC_FAILURE);
            }
            if (close(fildes[1]) == -1) {
                exit(EXEC_FAILURE);
            }
        }
        if (current > 0) {
            if (close(0) == -1) {
                exit(EXEC_FAILURE);
            }
            if (dup(current_pipe) == -1) {
                exit(EXEC_FAILURE);
            }
            if (close(current_pipe) == -1) {
                exit(EXEC_FAILURE);
            }
        }
        set_IO(redirs);
        execute(args);
    }
    else if (pr>0) {
        if (fildes[1] != 1) {
             while ( -1 == close(fildes[1]) && errno == EINTR) ;
        }
        if (current_pipe >= 3) {
            while ( -1 == close(current_pipe) && errno == EINTR) ;
        }
        if (!is_background) {
    		foreground_pids[current] = pr;
    	}
    }
    return fildes[0];    
}

redir** get_redirs(command* com, redir** res) { //uzyskaj przekierowania dla danej komedny
    res[0] = NULL;
    res[1] = NULL;
    if (com->redirs == NULL) {
        return res;
    }
    redirseq* seq = com->redirs;
    redir* first = seq->r;
    do {
        if (IS_RIN(seq->r->flags)) {
            res[0] = seq->r;
        }
        else if (IS_ROUT(seq->r->flags) || IS_RAPPEND(seq->r->flags)) {
            res[1] = seq->r;
        }
        seq = seq->next;
    } while (seq->r != first);
    return res;
}

char** get_args(command* com, char** args) { //zdobądź args z danej komedny
    argseq* first = com->args;
    argseq* current_arg = first->next;
    args[0] = first->arg;
    int i = 1;
    for(i = 1 ; current_arg != first; i++) {
        args[i] = current_arg->arg;
        current_arg = current_arg->next;
    }
    args[i] = NULL;
    return args;
}

int execute_command(command* com, int current, int amount_of_all, int current_pipe, bool is_background) { //wykonaj komendę
    char* args[MAX_LINE_LENGTH/2 + 5];
    get_args(com, args); //uzyskaj args
    
    redir* redirs[2];
    get_redirs(com, redirs); //uzyskaj przekierowania

    builtin_pair* command = get_shell_command(args[0]); //czy jest to komedna shella
    if (command == NULL) {
        return make_child(args, redirs, current, amount_of_all, current_pipe, is_background);
    }
    else{
        command->fun(args);
        return -1;
    }  
}

void execute_pipeline(pipeline* line) { //iteracja po pipeline
    if (line == NULL || line->commands == NULL) {
        return;
    }
    command* commands[MAX_LINE_LENGTH/2 + 5];
    commands[0] = line->commands->com;
    commandseq* current_command = line->commands->next;

    int amount_of_commands = 1; //odczytaj wszystkie komedny w pipeline
    for(amount_of_commands = 1 ; current_command != line->commands; amount_of_commands++) {
        if (current_command->com == NULL) {
            print_error(NULL, SYNTAX_ERROR_STR);
            return;
        }
        commands[amount_of_commands] = current_command->com;
        current_command = current_command->next;
    }
    commands[amount_of_commands] = NULL;
    
    if (commands[0] == NULL) {
        if (amount_of_commands > 1) {
        	print_error(NULL, SYNTAX_ERROR_STR);
        }
        return;
    }

    int current_pipe = -1;
    for (int i = 0; i < amount_of_commands; i++) { //wykonaj każda komendę
        current_pipe = execute_command(commands[i], i, amount_of_commands - 1, current_pipe, INBACKGROUND & line->flags);
    }

    if (!(INBACKGROUND & line->flags) && current_pipe != -1) {
    	foreground_counter += amount_of_commands;
	}
    
    while (foreground_counter != 0) { //poczekaj na zakończenie procesów	
    	sigsuspend(&mask_sigsuspend);
    }
}

void execute_pipelineseq(pipelineseq* ln) { //iteracja po pipelineseq
    if (ln == NULL) {
        print_error(NULL, SYNTAX_ERROR_STR);
        return;
    }
    pipelineseq* current_pipelineseq = ln;
    do {
        execute_pipeline(current_pipelineseq->pipeline);
        current_pipelineseq = current_pipelineseq->next;
    } while (current_pipelineseq != ln);
}

void work(char* buf, bool tooLongLine) { //rozpocznij wykonywanie lini
    if(tooLongLine) {
        print_error(NULL, SYNTAX_ERROR_STR);
        return;
    }
    pipelineseq* ln;
    command* com;
    ln = parseline(buf);
    
    execute_pipelineseq(ln); 
}

void prompt(bool show, bool special) { //wypisz prompt oraz informajcę o zakończonych procesach w tle
    if (!show || !special) {
        return;
    }
    char str[(60 + sizeof(int) + sizeof(pid_t)) * 32]; 
    for (int i = 0; i < 32; i++) {
    	if (background[i].pid == 0) {
    		break;
    	}
    	if (WIFSIGNALED(background[i].status)) {
    		sprintf(str, "%s%s%d%s%d%s\n", str, "Background process ", background[i].pid,
    			" terminated. (killed by signal ", WTERMSIG(background[i].status), ")");
    	}
    	else {
    		sprintf(str, "%s%s%d%s%d%s\n", str, "Background process ", background[i].pid,
    			" terminated. (exited with status ", WEXITSTATUS(background[i].status), ")");
    	}
    	background[i].pid = 0;
    }
    sprintf(str, "%s%s", str, PROMPT_STR);
   	my_write(1, str, strlen(str));
   	str[0] = 0;
}

int distance(char* begin, char* end) { //zwróć odległość między znakami
    if (end == NULL) {
        return MAX_LINE_LENGTH+5;
    } 
    return (int) (end-begin)+1;
}

bool get_fstatus() { //zwróć status działania w terminalu
    struct stat buffer;
    int status;
    status = fstat(0, &buffer);
    return S_ISCHR(buffer.st_mode);
}


int main (int argc, char *argv[]) {
	change_signals_handling();
    int shift = 0;
    char buf [2 * MAX_LINE_LENGTH + 5];
    bool tooLongLine = false;
    bool showPrompt = true;
    char* beginOfLine = buf;
    char* endOfLine;
    ssize_t readChars;
    bool fstatus = get_fstatus();

    sigemptyset(&mask_sigsuspend);
    sigemptyset(&mask_sigchld);
    sigaddset(&mask_sigchld, SIGCHLD);
    
    while (true) {
        prompt(showPrompt, fstatus);
        unlock_sigchld();
        readChars = read(0, buf + shift, MAX_LINE_LENGTH + 2);
        lock_sigchld();
        showPrompt = true;
        if (readChars == 0 || (readChars == -1 && errno != EINTR)) {
            break;  
        } 


        beginOfLine = buf;
        endOfLine = strchr (buf + shift, (int) '\n'); //znajdz piewrszy koniec lini
        while (distance(buf + shift, endOfLine) <= readChars) { 
            *endOfLine = 0;
            if (distance(beginOfLine, endOfLine) > MAX_LINE_LENGTH) { //sprawdz czy linia nie jest zadluga
                tooLongLine = true;
            }  
            work(beginOfLine, tooLongLine); //zacznij pracować na lini
            tooLongLine = false;
            beginOfLine = endOfLine + 1; //ustaw początek nowej lini za końcem obecnej
            endOfLine = strchr (beginOfLine, (int) '\n'); //znajdź nowy koniec lini
        }
        if (buf [shift + readChars - 1] != 0) {
            showPrompt = false;
        }
        int dis = distance(beginOfLine, buf + shift + readChars - 1);
        if (dis > 0) { //jeżeli zostały znaki, które nie zostały przypisane do lini
            if (dis > MAX_LINE_LENGTH) { //jezeli potencjalna linia będzie za długa
                tooLongLine = true;
                shift = 0; //ustaw początek reada na początek bufforu
            }
            else { 
                memmove(buf, beginOfLine, dis); //przepisz niewykorzystane znakki
                shift = dis;
            }
        } 
        else {
            shift = 0;
        }
    }



    if (beginOfLine < buf + shift + readChars) { //wrzuc nieprzypisane znaki po zakończeniu czytania jako ostatnią linie
        endOfLine = buf + shift + readChars;
        *endOfLine = 0;
        if (distance(beginOfLine, endOfLine) > MAX_LINE_LENGTH) {
            tooLongLine = true;
        }  
        work(beginOfLine, tooLongLine);
    }
}
