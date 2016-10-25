
int sn_ivsm_irq_to_qid(int irq);
int sn_ivsm_register_interrupt(unsigned int nvec);
long sn_ivsm_mmap(struct file *filp, struct vm_area_struct *vma);
int sn_ivsm_init(void);
void sn_ivsm_cleanup(void);
void *sn_ivsm_get_start(void);
long sn_ivsm_get_len(void);
void sn_ivsm_register_ih(void (*ih)(int itr, u32 msg));
