#ifndef	__DRIVER_FAT__H__
#define	__DRIVER_FAT__H__

/************************
 *			*
 *     Constantes	*
 *			*
 ************************/
/* Flags de cada entrada de directorio */
#define FAT_READ_ONLY	0x01
#define FAT_HIDDEN	0x02
#define FAT_SYSTEM	0x04
#define FAT_VOLUME_ID	0x08
#define FAT_DIRECTORY	0x10
#define FAT_ARCHIVE	0x20 
#define FAT_LFN		(FAT_READ_ONLY|FAT_HIDDEN|FAT_SYSTEM|FAT_VOLUME_ID)


/************************
 *			*
 *     Estructuras	*
 *			*
 ************************/

/* Entrada de directorio (sacado de Wikipedia) */
typedef	struct __attribute__((packed))
    {
	char		Name[8];
	char		Ext[3];
	__u8		FileAttributes;
	__u8		Reserverd;
	__u8		CerationTimeMS;
	__u16		CreationTime;
	__u16		CreationDate;
	__u16		LastAccessDate;
	__u16		StartClusterH;
	__u16		ModificationTime;
	__u16		ModificationDate;
	__u16		StartClusterL;
	__le32		FileSize;
    }	TDirEntryFAT;


/********************************
 *				*
 *	 Clase TDriverFAT	*
 *				*
 ********************************/
class TDriverFAT : public TDriverBase
{
public:
					TDriverFAT(const unsigned char *DiskData, unsigned LongitudDiskData);
	virtual				~TDriverFAT();

protected:
	/* Funciones a implementar por el alumno */
	virtual int			LevantarDatosSuperbloque();
	virtual int 			ListarDirectorio(const char *Path, std::vector<TEntradaDirectorio> &Entradas);
	virtual int 			LeerArchivo(const char *Path, unsigned char *&Data, unsigned &DataLen);
	virtual const unsigned char* PunteroACluster(unsigned int NroCluster);


    /* Mis funciones pples */
    /*Funcion ListarDirectorio (2):: navegadora*/
    virtual int ListarDirectorio(std::vector<unsigned int> &ClustersDirActual, const char *SubDirs, std::vector<TEntradaDirectorio> &Entradas); 
    /*Funcion ListarDirectorio (3):: lectora */
    virtual int ListarDirectorio(std::vector<unsigned int> &Clusters, std::vector<TEntradaDirectorio> &Entradas);


    /* Mis funciones auxiliares */
    /* PopPathComponent :: separar un path en dos partes tomando como separador el primer slash encontrado */
    void PopPathComponent(const char *pPath, std::string& pComponente, std::string& pResto);
    /*FatTimeToTimeT :: para convertir las fechas de FAT a time stamp */
    time_t FatTimeToTimeT(__u16 pFatDate, __u16 pFatTime);
    /* Función auxiliar para parsear una entrada de 32 bytes. Rellena una struct tipo TEntradaDirectorio*/
    bool ParsearEntradaFAT(TDirEntryFAT* pRawEntry, TEntradaDirectorio& pEntrada);
    /*Sigue la cadena de la FAT y devuelve la lista de clusters.*/
    virtual int BuscarCadenaDeClusters(unsigned int PrimerCluster,  __u64 Longitud, std::vector<unsigned> &Clusters);
    

    
   
};

#endif
