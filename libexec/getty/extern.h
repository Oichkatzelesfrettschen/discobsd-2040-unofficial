void	 gettable(char *, char *, char *);
void	 gendefaults(void);
void	 setdefaults(void);
void	 setchars(void);
long	 setflags(int);
struct sgttyb;
void	 splitflags(long, struct sgttyb *, int *);
void	 applyflags(struct sgttyb *, int);
void	 applymode(long, struct sgttyb *, int);
void	 resolveparity(void);
void	 edithost(char *);
long	 speed(long);
void	 makeenv(char *[]);
char	*portselector(void);
char	*autobaud(void);

void	 get_date(char *);
