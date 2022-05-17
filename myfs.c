/* Sistem de fisiere virtual - liant intre kernel si alte sisteme de fisiere */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

MODULE_DESCRIPTION("Simple no-dev filesystem");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define MYFS_BLOCKSIZE		4096
#define MYFS_BLOCKSIZE_BITS	12
#define MYFS_MAGIC		0xbeefcafe
#define LOG_LEVEL		KERN_ALERT


/* 	Superblock 
	- implementat de fiecare sistem de fisiere montat
	- stocheaza informatii specifice sistemului de fisiere
	- sistemele virtuale il genereaza dinamic si il stocheaza in memorie */

/*	Inode
	- identifica unic un fisier
	- detine informatia necesara kernelului de o opera pe un fisier
	- informatii statice, generale */

/*	Dentry
	- face legatura intre inode-uri si nume de fisiere
	- este o parte dintr-o cale */

/* 	File
	- reprezinta un fisier deschis de un proces
	- este stocat doar in memorie */


/* Declaratiiele functiilor care fac parte din inode_operations */
static int myfs_mknod(struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev);
static int myfs_create(struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl);
static int myfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);


/* Structura care contine operatiile din superbloc */
static const struct super_operations myfs_ops = {
	.drop_inode = generic_drop_inode,	// apelata cand ultima referinta la un inode este stearsa
	.statfs		= simple_statfs			// apelata ca sa se obtina statistici ale sistemului de fisiere
};

/* Structura care contine operatiile pe directoare asociate cu Inode*/
static const struct inode_operations myfs_dir_inode_operations = {
	.create 	= myfs_create,		// apelat de apelurile de sistem creat() si open()
	.lookup 	= simple_lookup,	// cauta un inode intr-un director
	.link 		= simple_link,		// apelat de link() si creaza un harlink
	.unlink 	= simple_unlink,	// apelat de unlink() si sterge inode-ul din dentry-ul specificat
	.mkdir 		= myfs_mkdir,		// apelat de apelul de sistem mkdir()
	.rmdir 		= simple_rmdir,		// apelat de rmdir() pentru a sterge directorul din dentry-ul specificat
	.mknod 		= myfs_mknod,		// apelat de apelul de sistem mknod()
	.rename 	= simple_rename		// apelat pentru a muta fisierul intr-un alt director si sub alt nume
};

/* Structura care contine operatiile pe fisiere asociate cu File*/
static const struct file_operations myfs_file_operations = {
	.read_iter 	= generic_file_read_iter,		// citeste din fisierul primit numarul dat de bytes
	.write_iter = generic_file_write_iter,		// scrie in fisierul primit numarul dat de bytes
	.llseek 	= generic_file_llseek,			// updateaza pointerul fisierului la offset-ul primit
	.mmap 		= generic_file_mmap				// mapeaza fisierului in spatiul de adresa primit
};

/* Structura care contine operatiile pe fisiere asociate cu Inode */
static const struct inode_operations myfs_file_inode_operations = {
	.setattr = simple_setattr,		// apelat de m notify_change() cand un inode a fost schimbat include si truncate
	.getattr = simple_getattr		// apelat de sistemul de fisiere cand observa ca un inode trebuie actualizat
};

/* Structura care contine operatiile din address space */
static const struct address_space_operations myfs_aops = {
	.readpage		= simple_readpage,		// citeste o pagina din address space
	.write_begin	= simple_write_begin,	// scrie o pagina in
	.write_end		= simple_write_end		// address space
};


/* 
	Initializarea unui inode 
	-> dir - directoriul parinte
*/
struct inode *myfs_get_inode(struct super_block *sb, const struct inode *dir,
		int mode)
{
	struct inode *inode = new_inode(sb);  				// alocarea unui inode pentru superblocul sb

	if (!inode)
		return NULL;

	inode_init_owner(inode, dir, mode);					// initializeaza uid, gid, modul pentru un inode nou
	inode -> i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);	// ultimul timp de acces, modificare continut si schimbare inode initializare la timpul curent

	// if(!dir)		// inode radacina
	//	 inode -> i_ino = 1;
	// else
	inode -> i_ino = get_next_ino();					// asignarea unui numar de inode disponibil
	inode -> i_mapping -> a_ops = &myfs_aops;			// initializarea operatiilor din address space

	if (S_ISDIR(mode)) {								// daca este director
		inode -> i_op = &myfs_dir_inode_operations; 	// initializarea operatiilor pe inode pentru directoare
		inode -> i_fop = &simple_dir_operations;		// initializarea operatiilor pe file pentru directoare
		inc_nlink(inode);								// incrementarea numarului de referinte pentru directoare
	}

	if(S_ISREG(mode)){			// daca este fisier obisnuit
		inode -> i_op = &myfs_file_inode_operations;	// initializarea operatiilor pe inode pentru fisiere obsinuite
		inode -> i_fop = &myfs_file_operations;			// initializarea operatiilor pe file pentru fisiere obsinuite
	}

	return inode;
}


/* Apelat de mknod() pentru a crea un fisier special */
static int myfs_mknod(struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev){
	
	struct inode * inode = myfs_get_inode(dir->i_sb, dir, mode);	// alocam un inode
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);								// completeaza informatiile unui dentry
		dget(dentry);												// incrementeaza referinta
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);			// initializem timpii de modificare
	}
	return error;
}

/* Apelat de creat() si open() pentru a crea un fisier normal */
static int myfs_create(struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl){
	return myfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

/* Apelat de mkdir() pentru a crea un director */
static int myfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode){
	int retval = myfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);		// incrementeaza referintele directorului parinte
	return retval;
}


/* 
	Initializarea superblocului si a inode-ului radacina
	-> struct super_block *sb - superblocul de initializat	
*/
static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct dentry *root_dentry;

	sb -> s_blocksize = MYFS_BLOCKSIZE;				// dimensiunea blocului - block device
	sb -> s_blocksize_bits = MYFS_BLOCKSIZE_BITS;	// dimensiunea blocului in biti
	sb -> s_magic = MYFS_MAGIC;						// numarul magic al sistemului de fisiere - identificarea tipului de fisier
	sb -> s_op = &myfs_ops;							// operatiile superblocului
	sb -> s_maxbytes = MAX_LFS_FILESIZE;			// dimensiunea maxima a unui fisier

	/* Initierea inode-ului radacina
	   director si drepturi de acces (755) */
	root_inode = myfs_get_inode(sb, NULL,
			S_IFDIR | S_IRWXU | S_IRGRP |
			S_IXGRP | S_IROTH | S_IXOTH);

	printk(LOG_LEVEL "root inode has %d link(s)\n", root_inode->i_nlink);

	if (!root_inode)
		return -ENOMEM;

	root_dentry = d_make_root(root_inode);	// alocare dentry radacina
	if (!root_dentry)
		goto out_no_root;
	sb->s_root = root_dentry;	// directorul unde a fost montat sistemul	

	return 0;

out_no_root:
	iput(root_inode);	// decrementeaza numarul de referinte si elibereaza memoria inode-ului daca e necesar
	return -ENOMEM;
}


/* 	
	Apelata la montarea sistemului de fisiere 
	-> returneaza un dentry ce reprezinta directorul unde a fost montat sistemul
*/
static struct dentry *myfs_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, myfs_fill_super);	// monteaza un sistem de fisiere fara un device fizic
}

/*
	Structura care descrie proprietatile un sistem de fisiere 
   	-> o singura structura asociata unui sistem indiferent de numarul de instante
*/
static struct file_system_type myfs_fs_type = {
	.owner		= THIS_MODULE,			// modulul care detine sistemul de fisiere
	.name		= "myfs",				// numele sistemului de fisiere
	.mount		= myfs_mount,			// citeste datele in superbloc la montarea sistemului de fisiere		
	.kill_sb	= kill_litter_super		// elibereaza superblocul din memorie si demonteaza un sistem de fisiere fara un device fizic
};


/* Inregistrarea sistemului de fisiere la incarcarea modulului */
static int __init myfs_init(void)
{
	int err = register_filesystem(&myfs_fs_type);
	if (err) {
		printk(LOG_LEVEL "register_filesystem failed\n");
		return err;
	}
	return 0;
}


/* Anularea inregistrarii sistemului de fisiere la scoaterea modulului */
static void __exit myfs_exit(void)
{
	unregister_filesystem(&myfs_fs_type);
}

module_init(myfs_init);
module_exit(myfs_exit);