void	 gettable(char *, char *, char *);
void	 gendefaults();
void	 setdefaults();
void	 setchars();
long	 setflags(int);
struct sgttyb;
void	 splitflags(long, struct sgttyb *, int *);
void	 applyflags(struct sgttyb *, int);
void	 applymode(long, struct sgttyb *, int);
void	 resolveparity(void);
void	 edithost(char *);
long	 speed(long);
void	 makeenv(char *[]);
char	*portselector();
char	*autobaud();

void	 get_date(char *);
