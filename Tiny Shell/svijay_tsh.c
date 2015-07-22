/* 
 * tsh - A tiny shell program with job control
 * 
 * Sudhir Kumar Vijay <svijay@andrew.cmu.edu>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t job_list[MAXJOBS]; /* The job list */

struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG} builtins;
};

/* End global variables */

/* Function prototypes */
void eval(char *cmdline);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Newly added functions */
void wait_state(pid_t pid);
int  builtin_command(struct cmdline_tokens* tok_ptr);
void builtin_jobs_handler(struct cmdline_tokens* tok_ptr);
void builtin_bg_handler(struct cmdline_tokens* tok_ptr) ;
void builtin_fg_handler(struct cmdline_tokens* tok_ptr) ;
struct job_t* extract_job_val (struct cmdline_tokens* tok_ptr);

/* Error checking wrapper functions used in csapp.h */
pid_t Fork(void);
void Sigdelset(sigset_t *set, int signum);
void Sigaddset(sigset_t *set, int signum);
void Sigfillset(sigset_t *set);
void Sigemptyset(sigset_t *set);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
pid_t Waitpid(pid_t pid, int *iptr, int options) ;
void Setpgid(pid_t pid, pid_t pgid);
void Kill(pid_t pid, int signum) ;
void Execve(const char *filename, char *const argv[], char *const envp[]) ;
int Open(const char *pathname, int flags) ;
void Close(int fd) ;
int Dup2(int fd1, int fd2); 

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *job_list);
int maxjid(struct job_t *job_list); 
int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *job_list, pid_t pid); 
pid_t fgpid(struct job_t *job_list);
struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
struct job_t *getjobjid(struct job_t *job_list, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *job_list, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int 
main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];    /* cmdline for fgets */
    int emit_prompt = 1;      /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(job_list);


    /* Execute the shell's read/eval loop */
    while (1) {

        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { 
            /* End of file (ctrl-d) */
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            exit(0);
        }
        
        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';
        
        /* Evaluate the command line */
        eval(cmdline);
        
        fflush(stdout);
        fflush(stdout);
    } 
    
    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void 
eval(char *cmdline) 
{
    int fd_outfile;
    int fd_infile;
    int bg;              /* should the job run in bg or fg? */
    int builtin;
   
    struct cmdline_tokens tok;
    struct cmdline_tokens* tok_ptr;
    
    tok_ptr = &tok;
    
    pid_t pid;
    sigset_t mask;
    
    /* Parse command line */
    bg = parseline(cmdline, &tok); 

    if (bg == -1) /* parsing error */
        return;
    if (tok.argv[0] == NULL) /* ignore empty lines */
        return;

    /* Evaluating if builtin function or not */
    builtin = builtin_command(tok_ptr);

    /* Handling the case where command is not built */
    if (builtin == 0){
        /* Blocking signals */
        Sigemptyset (&mask);
        Sigaddset   (&mask, SIGCHLD); 
        Sigaddset   (&mask, SIGINT); 
        Sigaddset   (&mask, SIGTSTP);
        Sigprocmask (SIG_BLOCK, &mask, NULL); 

        if ((pid = Fork()) == 0) {
            Sigprocmask(SIG_UNBLOCK, &mask, NULL); 
            Setpgid(0, 0);

            if (tok_ptr->outfile != NULL) {
                /* Output file argument */
                fd_outfile = Open (tok_ptr->outfile, O_RDWR);
                Dup2(fd_outfile, 1);
                Close(fd_outfile);
            }
 
            if (tok_ptr->infile != NULL) {
                /* Input file argument */
                fd_infile = Open(tok_ptr->infile, O_RDONLY);
                Dup2(fd_infile, 0);
                Close(fd_infile);
            }

            /* Restoring default signal states before execing */
            Signal(SIGINT,  SIG_DFL);
            Signal(SIGTSTP, SIG_DFL);  
            Signal(SIGCHLD, SIG_DFL);  

            if (execve(tok.argv[0], tok.argv, environ) < 0) {
                printf("%s: Command not found \n", cmdline);
                fflush(stdout);
                return;
            }
        }

        /* Adding jobs for the executed thread */
        if(bg == 1) {
            addjob(job_list, pid, BG , cmdline);
            /* Unblocking signals after adding job */
            Sigprocmask(SIG_UNBLOCK, &mask, NULL);
            printf("[%d] (%d) %s \n", pid2jid(pid), pid, cmdline);
            fflush(stdout);
        } else if (bg == 0) {
            addjob(job_list, pid, FG , cmdline);
            wait_state(pid);
            /* Unblocking signals after wait state*/
            Sigprocmask(SIG_UNBLOCK, &mask, NULL);
        } 
    }
    return;
}
/* builtin_command: Checks whether input is a built-in command or not
 * 
 * If it is recognized as a builtin command, then this function calls the
 * appropriate function that executes a particular artion.
 * If the function is recognized as builtin, this returns back to the calling
 * function 'eval' where the corresponding exec action is performed.
 */

int builtin_command(struct cmdline_tokens* tok_ptr)
{
    /* Handling builtin quit case */
    if(tok_ptr->builtins == BUILTIN_QUIT) {
        exit(0);
    }
    
    /* Handling builtin jobs case */
    if(tok_ptr->builtins == BUILTIN_JOBS) {
        builtin_jobs_handler(tok_ptr);
        return 1;
    }    
    
    /* Handling builtin bg case */
    if(tok_ptr->builtins == BUILTIN_BG) {
        builtin_bg_handler(tok_ptr);
        return 1;
    }

    /* Handling builtin fg case */
    if(tok_ptr->builtins == BUILTIN_FG) {
        builtin_fg_handler(tok_ptr);
        return 1;
    }

    /* Handling builtin none case */
    if(tok_ptr->builtins == BUILTIN_NONE) {
        return 0;
    }
    return 0;
}


/* builtin_jobs_handler : 'jobs' called, displaying jobs 
 *          
 * This function handles the built-in display jobs case.
 */
void builtin_jobs_handler(struct cmdline_tokens* tok_ptr)
{
   int dup_backup_o;
   int fd_out;
   
   /* Taking care of the case where outfile is given ('>' redirection) */ 
   if ((tok_ptr->outfile) != NULL) {
        /* Backing up file description handler */
        dup_backup_o = dup(1);

        /* Opening the file with read/write permissions */
        fd_out = Open (tok_ptr->outfile, O_RDWR);
        Dup2(fd_out, 1);
     
        /* Calling built-in listjobs */
        listjobs(job_list, 1);
        Close(fd_out);
        
        /* Restoring file description handler*/
        Dup2(dup_backup_o, 1);
    }

    /* Taking care of stdout case */
    if ((tok_ptr->infile == NULL) && (tok_ptr->outfile == NULL)){
        listjobs(job_list, 1);
    }
    
    /* Note that the infile case does not exist ('<' redirection) */
    return;
}


/* builtin_bg_handler: Input format on shell should be in the 
 *                     form of 'bg %jid' or 'bg pid'
 * 
 * Changes stopped job into running background job */
void builtin_bg_handler(struct cmdline_tokens* tok_ptr) {
    int bg_pid = 0;
    struct job_t* bg_job = NULL;       
   
    bg_job = extract_job_val(tok_ptr);
    
    if(bg_job!=NULL){
        bg_pid = bg_job->pid;
        Kill(-bg_pid, SIGCONT);
        bg_job->state = BG;
        printf("[%d] (%d) %s \n", bg_job->jid, bg_job->pid, bg_job->cmdline);
        fflush(stdout);
    } 
    return; 
}

/* builtin_fg_handler: Input format on shell should be in the 
 *                     form of 'fg %jid' or 'fg pid'
 * 
 * Changes stopped job into running foreground job */
void builtin_fg_handler(struct cmdline_tokens* tok_ptr) {
    int fg_pid = 0;
    struct job_t* fg_job = NULL;       
   
    fg_job = extract_job_val(tok_ptr);
    
    if(fg_job!=NULL){
        fg_pid = fg_job->pid;
        fg_job-> state = FG;
        Kill(-fg_pid,  SIGCONT);
        wait_state(fg_pid);
    } 
    return; 
}

/* extract_job_val: Function that extracts the job corresponding
 *                  to input arguments. If job doesn't exist or
 *                  in case of error, returns NULL. 
 */
struct job_t* extract_job_val (struct cmdline_tokens* tok_ptr) {
    int inp_jid = 0;
    pid_t inp_pid = 0;

    struct cmdline_tokens tok;
    tok = *tok_ptr;

    char *inp_argv = tok.argv[1];
    struct job_t* ret_job = NULL;       

    /* Returning back in case of invalid arguments */
    if (inp_argv == NULL){
            printf("fg command requires PID or JID argument \n");
            return NULL;
    }
    /* Extracting job ID from text */
    else if (tok.argv[1][0] == '%'){
        inp_jid = atoi(&inp_argv[1]);
        ret_job = getjobjid(job_list, inp_jid); 
        if (ret_job==NULL){
            printf("%s: No such job \n", inp_argv);
            return NULL;
        }
    } else {
    /* Extracting process ID from text */
        inp_pid = atoi(inp_argv);
        ret_job = getjobjid(job_list, inp_pid); 
        if (ret_job==NULL){
            printf("(%d): No such process \n", inp_pid);
            return NULL;
        }
    }
   return ret_job;
}


/* wait_state: Function that implements wait-state for 
 *             foreground job. 
 */ 
void wait_state(pid_t pid){
    /* Creating a mask to prevent 
     * termination during wait. */
    sigset_t    wait_mask;
    Sigfillset  (&wait_mask);
    Sigdelset   (&wait_mask, SIGCHLD); 
    Sigdelset   (&wait_mask, SIGINT); 
    Sigdelset   (&wait_mask, SIGTSTP);
 
    while (pid == fgpid(job_list)){
       /* Suspending process 
        * Not checking for error since sigsuspend
        * may return valid wrong values */   
       sigsuspend(&wait_mask); 
    }
    return; 
}

/* Error checking wrappers taken from csapp.h */
pid_t Fork(void) 
{
    pid_t pid;
    if ((pid = fork()) < 0)
	unix_error("Fork error");
    return pid;
}

void Execve(const char *filename, char *const argv[], char *const envp[]) 
{
    if (execve(filename, argv, envp) < 0)
	unix_error("Execve error");
}

void Kill(pid_t pid, int signum) 
{
    int rc;

    if ((rc = kill(pid, signum)) < 0)
	unix_error("Kill error");
}

void Setpgid(pid_t pid, pid_t pgid) {
    int rc;

    if ((rc = setpgid(pid, pgid)) < 0)
	unix_error("Setpgid error");
    return;
}

pid_t Waitpid(pid_t pid, int *iptr, int options) 
{
    pid_t retpid;

    if ((retpid  = waitpid(pid, iptr, options)) < 0) 
	unix_error("Waitpid error");
    return(retpid);
}

void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (sigprocmask(how, set, oldset) < 0)
	unix_error("Sigprocmask error");
    return;
}

void Sigemptyset(sigset_t *set)
{
    if (sigemptyset(set) < 0)
	unix_error("Sigemptyset error");
    return;
}

void Sigfillset(sigset_t *set)
{ 
    if (sigfillset(set) < 0)
	unix_error("Sigfillset error");
    return;
}

void Sigaddset(sigset_t *set, int signum)
{
    if (sigaddset(set, signum) < 0)
	unix_error("Sigaddset error");
    return;
}

void Sigdelset(sigset_t *set, int signum)
{
    if (sigdelset(set, signum) < 0)
	unix_error("Sigdelset error");
    return;
}

int Open(const char *pathname, int flags) 
{
    int rc;

    if ((rc = open(pathname, flags))  < 0)
	unix_error("Open error");
    return rc;
}

void Close(int fd) 
{
    int rc;
    if ((rc = close(fd)) < 0)
	unix_error("Close error");
}

int Dup2(int fd1, int fd2) 
{
    int rc;
    if ((rc = dup2(fd1, fd2)) < 0)
	unix_error("Dup2 error");
    return rc;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   tok:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters 
 *             enclosed in single or double quotes are treated as a single
 *             argument. 
 * Retur`ns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job  
 *  -1:        if cmdline is incorrectly formatted
 * 
 * Note:       The string elements of tok (e.g., argv[], infile, outfile) 
 *             are statically allocated inside parseline() and will be 
 *             overwritten the next time this function is invoked.
 */
int 
parseline(const char *cmdline, struct cmdline_tokens *tok) 
{

    static char array[MAXLINE];          /* holds local copy of command line */
    const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
    char *buf = array;                   /* ptr that traverses command line */
    char *next;                          /* ptr to the end of the current arg */
    char *endbuf;                        /* ptr to end of cmdline string */
    int is_bg;                           /* background job? */

    int parsing_state;                   /* indicates if the next token is the
                                            input or output file */

    if (cmdline == NULL) {
        (void) fprintf(stderr, "Error: command line is NULL\n");
        return -1;
    }

    (void) strncpy(buf, cmdline, MAXLINE);
    endbuf = buf + strlen(buf);

    tok->infile = NULL;
    tok->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;
    tok->argc = 0;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn (buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (tok->infile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_INFILE;
            buf++;
            continue;
        }
        if (*buf == '>') {
            if (tok->outfile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_OUTFILE;
            buf ++;
            continue;
        }

        if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr (buf, *(buf-1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn (buf, delims);
        }
        
        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            (void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
            return -1;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
        case ST_NORMAL:
            tok->argv[tok->argc++] = buf;
            break;
        case ST_INFILE:
            tok->infile = buf;
            break;
        case ST_OUTFILE:
            tok->outfile = buf;
            break;
        default:
            (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
            return -1;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (tok->argc >= MAXARGS-1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) {
        (void) fprintf(stderr,
                       "Error: must provide file name for redirection\n");
        return -1;
    }

    /* The argument list must end with a NULL pointer */
    tok->argv[tok->argc] = NULL;

    if (tok->argc == 0)  /* ignore blank line */
        return 1;

    if (!strcmp(tok->argv[0], "quit")) {                 /* quit command */
        tok->builtins = BUILTIN_QUIT;
    } else if (!strcmp(tok->argv[0], "jobs")) {          /* jobs command */
        tok->builtins = BUILTIN_JOBS;
    } else if (!strcmp(tok->argv[0], "bg")) {            /* bg command */
        tok->builtins = BUILTIN_BG;
    } else if (!strcmp(tok->argv[0], "fg")) {            /* fg command */
        tok->builtins = BUILTIN_FG;
    } else {
        tok->builtins = BUILTIN_NONE;
    }

    /* Should the job run in the background? */
    if ((is_bg = (*tok->argv[tok->argc-1] == '&')) != 0)
        tok->argv[--tok->argc] = NULL;

    return is_bg;
}


/*****************
 * Signal handlers
 *****************/
/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The 
 *     handler reaps all available zombie children, but doesn't wait 
 *     for any other currently running children to terminate.  
 */
void 
sigchld_handler(int sig) 
{   pid_t pid;
    int pid_status;
    struct job_t* current_fgjobpid;
    
    /* Short variable name for 80 char autolab limit 
     * Variables to store WTERMSIG/WSTOPSIG values */
    int term = 0; 
    int stop = 0; 

    int jid = 0;
    pid_t current_fgpid;

    /* Blocking signals so that handler does not
     * get preempted */
    sigset_t   sigchld_mask;
    sigemptyset(&sigchld_mask);
    sigaddset  (&sigchld_mask, SIGCHLD); 
    sigaddset  (&sigchld_mask, SIGINT); 
    sigaddset  (&sigchld_mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &sigchld_mask, NULL); 

    while((pid = waitpid(-1, &pid_status, (WNOHANG|WUNTRACED)))>0) {
        if(WIFEXITED(pid_status)){
            deletejob(job_list, pid);
        }
        else if(WIFSIGNALED(pid_status)){
            term = WTERMSIG(pid_status); 
            jid = pid2jid(pid);
            printf("Job [%d] (%d) terminated by signal %d \n", jid,pid,term);
            fflush(stdout);
            deletejob(job_list, pid);
        }
        else if(WIFSTOPPED(pid_status)){
            stop = WSTOPSIG(pid_status);
            jid = pid2jid(pid);
            printf("Job [%d] (%d) stopped by signal %d \n", jid,pid,stop);
            fflush(stdout);
            current_fgpid    = fgpid(job_list) ;
            current_fgjobpid =  getjobpid(job_list, current_fgpid);
            current_fgjobpid->state = ST; 
        }
    }
 
    sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL); 
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void 
sigint_handler(int sig) 
{   
    /* Blocking signals so that handler does not
     * get preempted */
    sigset_t     siginit_mask;
    sigemptyset (&siginit_mask);
    sigaddset   (&siginit_mask, SIGCHLD); 
    sigaddset   (&siginit_mask, SIGINT); 
    sigaddset   (&siginit_mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &siginit_mask, NULL); 

    pid_t current_fgpid;
    current_fgpid = fgpid(job_list);
    
    if (current_fgpid != 0) {
        if(kill(-current_fgpid, SIGINT) < 0){
            app_error("Kill error");
            sigprocmask(SIG_UNBLOCK, &siginit_mask, NULL); 
            return;
        }
    }
    sigprocmask(SIG_UNBLOCK, &siginit_mask, NULL); 
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void 
sigtstp_handler(int sig) 
{
    /* Blocking signals so that handler does not
     * get preempted */
    sigset_t   sigtstp_mask;
    sigemptyset(&sigtstp_mask);
    sigaddset  (&sigtstp_mask, SIGCHLD); 
    sigaddset  (&sigtstp_mask, SIGINT); 
    sigaddset  (&sigtstp_mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &sigtstp_mask, NULL); 

    pid_t current_fgpid;
    current_fgpid = fgpid(job_list);
    
    if (current_fgpid != 0) {
        if(kill(-current_fgpid, SIGTSTP)<0){
            app_error("Kill error");
            sigprocmask(SIG_UNBLOCK, &sigtstp_mask, NULL); 
            return;
        }
    }

    sigprocmask(SIG_UNBLOCK, &sigtstp_mask, NULL); 
    return; 
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void 
clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void 
initjobs(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&job_list[i]);
}

/* maxjid - Returns largest allocated job ID */
int 
maxjid(struct job_t *job_list) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid > max)
            max = job_list[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int 
addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) {
            job_list[i].pid = pid;
            job_list[i].state = state;
            job_list[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(job_list[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n",
                       job_list[i].jid,
                       job_list[i].pid,
                       job_list[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int 
deletejob(struct job_t *job_list, pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            clearjob(&job_list[i]);
            nextjid = maxjid(job_list)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t 
fgpid(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].state == FG)
            return job_list[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t 
*getjobpid(struct job_t *job_list, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid)
            return &job_list[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *job_list, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid == jid)
            return &job_list[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int 
pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid) {
            return job_list[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void 
listjobs(struct job_t *job_list, int output_fd) 
{
    int i;
    char buf[MAXLINE];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE);
        if (job_list[i].pid != 0) {
            sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            switch (job_list[i].state) {
            case BG:
                sprintf(buf, "Running    ");
                break;
            case FG:
                sprintf(buf, "Foreground ");
                break;
            case ST:
                sprintf(buf, "Stopped    ");
                break;
            default:
                sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
                        i, job_list[i].state);
            }
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            sprintf(buf, "%s\n", job_list[i].cmdline);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void 
usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void 
unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void 
app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t 
*Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void 
sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

