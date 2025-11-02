#include "all_heads.h"
#include <string>   // Para std::string
#include <cstring>  // Para strchr
#include <ctime> // Para struct tm y mktime()

// Estructura del Boot Sector (BPB - BIOS Parameter Block) para FAT12
typedef struct __attribute__((packed)) { //usamos packed para evitar el padding
    __u8  Jump[3]; // Jump x si el disco estar formateado en Fat
    char  OEM[8]; // Nombre del SO con el que se formateo
    __u16 BytesPorSector; 
    __u8  SectoresPorCluster;
    __u16 SectoresReservados;
    __u8  CopiasFAT;
    __u16 EntradasRootDir;      // Limite para FAT12
    __u16 TotalSectores16;    // (si entra; si es cero, no es FAT12)
    __u8  MediaDescriptor;
    __u16 SectoresPorFAT;       
    __u16 SectoresPorTrack;
    __u16 NumeroDeHeads;
    __u32 SectoresOcultos;
    __u32 TotalSectores32;
    // ... aquí irían más campos para FAT32, pero para FAT12/16 no los necesitamos
} TBiosParameterBlockFAT;

/********************************
 *				*
 *	 Clase TDriverFAT	*
 *				*
 ********************************/

/****************************************************************************************************************************************
 *																	*
 *							TDriverFAT :: TDriverFAT							*
 *																	*
 * OBJETIVO: Inicializar la clase recién creada.											*
 *																	*
 * ENTRADA: DiskData: Puntero a un bloque de memoria con la imágen del disco a analizar.						*
 *	    LongitudDiskData: Tamaño, en bytes, de la imágen a analizar.								*
 *																	*
 * SALIDA: Nada.															*
 *																	*
 ****************************************************************************************************************************************/
TDriverFAT::TDriverFAT(const unsigned char *DiskData, unsigned LongitudDiskData) : TDriverBase(DiskData, LongitudDiskData)
{
}


/****************************************************************************************************************************************
 *																	*
 *							TDriverFAT :: ~TDriverFAT							*
 *																	*
 * OBJETIVO: Liberar recursos alocados.													*
 *																	*
 * ENTRADA: Nada.															*
 *																	*
 * SALIDA: Nada.															*
 *																	*
 ****************************************************************************************************************************************/
TDriverFAT::~TDriverFAT()
{
}


/* =================== Funciones virtuales de implementación obligatorias =================== */
/**
 *  Devuelve un puntero directo al inicio del cluster solicitado.
 */

const unsigned char* TDriverFAT::PunteroACluster(unsigned int NroCluster)
{
    // Los clusters 0 y 1 no son punteros a datos.
    if (NroCluster < 2)
    {
        return nullptr;
    }

    // 1. Obtener los datos específicos de FAT
    TDatosFSFAT& fatData = this->DatosFS.DatosEspecificos.FAT;

    // 2. Calcular el tamaño del Root Directory (solo para FAT12/16)
    unsigned int sectoresRootDir = 0;
    if (this->DatosFS.TipoFilesystem != tfsFAT32)
    {
        unsigned int bytesDelRootDir = fatData.EntradasRootDir * 32;
        sectoresRootDir = (bytesDelRootDir + this->DatosFS.BytesPorSector - 1) / this->DatosFS.BytesPorSector;
    }
    
    // 3. Calcular el primer sector del área de datos (donde empieza el Cluster 2)
    unsigned int primerSectorDeDatos = fatData.SectoresReservados 
                                     + (fatData.CopiasFAT * fatData.SectoresPorFAT) 
                                     + sectoresRootDir;

    // 4. Calcular el sector de inicio para el cluster N
    // (NroCluster - 2) porque el Cluster #2 está en el offset 0 del área de datos.
    unsigned int sectorDelCluster = primerSectorDeDatos 
                                  + ((NroCluster - 2) * fatData.SectoresPorCluster);

    // 5. Devolver el puntero a ese sector usando la función de la clase base
    return this->PunteroASector(sectorDelCluster);
}


/* =================== Funciones auxilares  =================== */
void TDriverFAT::PopPathComponent(const char *pPath, std::string& pComponente, std::string& pResto)
{   
    //buffers iniciales
    pComponente = "";
    pResto = "";

    //chequear si el path esta vacio
    if (!pPath || pPath[0] == '\0')
    {
        return;}

    // buscar la posicion del primer '/' dentro del arreglo
    const char* primerSlash = strchr(pPath, '/');
    
    if (primerSlash == nullptr)
    {
        // si no hay '/' , el path entero esta completo
        pComponente = pPath;
    }
    else
    {
        // sino, separamos el arreglo en dos partes pos y pre slash
        //pre
        pComponente.assign(pPath, primerSlash - pPath);
        // post
        pResto = primerSlash + 1;
    }
}

time_t TDriverFAT::FatTimeToTimeT(__u16 pFatDate, __u16 pFatTime)
{
    struct tm t = {}; // Inicializar la estructura de tiempo a ceros

    // --- Decodificar la HORA (__u16 pFatTime) ---
    // Bits 15-11 (5 bits): Hora (0-23)
    t.tm_hour = (pFatTime >> 11) & 0x1F;
    // Bits 10-5 (6 bits): Minuto (0-59)
    t.tm_min = (pFatTime >> 5) & 0x3F;
    // Bits 4-0 (5 bits): Segundos (en incrementos de 2 seg, 0-29)
    t.tm_sec = (pFatTime & 0x1F) * 2;

    // --- Decodificar la FECHA (__u16 pFatDate) ---
    // Bits 15-9 (7 bits): Año (desde 1980)
    // tm_year es "años desde 1900", por eso sumamos 80 (1980 - 1900)
    t.tm_year = ((pFatDate >> 9) & 0x7F) + 80;
    // Bits 8-5 (4 bits): Mes (1-12)
    // tm_mon es "meses desde Enero" (0-11), por eso restamos 1
    t.tm_mon = ((pFatDate >> 5) & 0x0F) - 1;
    // Bits 4-0 (5 bits): Día (1-31)
    t.tm_mday = (pFatDate & 0x1F);

    // Indicar a mktime que determine el horario de verano automáticamente
    t.tm_isdst = -1; 

    // Convertir la estructura 'tm' a un 'time_t'
    return mktime(&t);
}

bool TDriverFAT::ParsearEntradaFAT(TDirEntryFAT* pRawEntry, TEntradaDirectorio& pEntrada){
    // 1. ignorar entradas de Nombre de Archivo Largo (LFN)
    if (pRawEntry->FileAttributes == FAT_LFN) {return false;}

    // 2. Parsear Nombre 
    std::string nombre = "";
    
    // son los primeros 8 chars (8 bytes)
    for (int i = 0; i < 8; i++)
    {
        if (pRawEntry->Name[i] == ' ') break;
        nombre += (char)pRawEntry->Name[i];
    }
    
    // 3. la extensión (3 bytes) 
    if (pRawEntry->Ext[0] != ' ')
    {
        nombre += ".";
        for (int i = 0; i < 3; i++)
        {
            if (pRawEntry->Ext[i] == ' ') break;
            nombre += (char)pRawEntry->Ext[i];
        }
    }
    
    // 4. Llenar la estructura genérica TEntradaDirectorio
    
    // Copiar el nombre y el tamaño del archivo
    pEntrada.Nombre = nombre.c_str(); 
    pEntrada.Bytes = pRawEntry->FileSize;

    // 5. Pasar los atributos de mi entrada FAT a los atributos (flags) de la struct generica
    pEntrada.Flags = 0; // Limpiamos flags anteriores
    if (pRawEntry->FileAttributes & FAT_READ_ONLY) pEntrada.Flags |= fedSOLO_LECTURA;
    if (pRawEntry->FileAttributes & FAT_HIDDEN)    pEntrada.Flags |= fedOCULTO;
    if (pRawEntry->FileAttributes & FAT_VOLUME_ID) pEntrada.Flags |= fedETIQUETA_VOLUMEN;
    if (pRawEntry->FileAttributes & FAT_SYSTEM)    pEntrada.Flags |= fedSISTEMA;
    if (pRawEntry->FileAttributes & FAT_DIRECTORY) pEntrada.Flags |= fedDIRECTORIO;
    if (pRawEntry->FileAttributes & FAT_ARCHIVE)   pEntrada.Flags |= fedARCHIVAR;
    
    // 6. Convertir las fechas de FAT a time_t
    pEntrada.FechaCreacion = this->FatTimeToTimeT(pRawEntry->CreationDate, pRawEntry->CreationTime);
    
    // último acceso no tiene hora, se asume 00:00
    pEntrada.FechaUltimoAcceso = this->FatTimeToTimeT(pRawEntry->LastAccessDate, 0);
    pEntrada.FechaUltimaModificacion = this->FatTimeToTimeT(pRawEntry->ModificationDate, pRawEntry->ModificationTime);

    // 7. Rellenar atributos especificos de FAT, ej:: en que cluster arranca el archivo?
    pEntrada.DatosEspecificos.FAT.PrimerCluster = pRawEntry->StartClusterL; // para FAT12, la cadena es solo un cluster
    
    return true; // Es una entrada válida
}

int TDriverFAT::BuscarCadenaDeClusters(unsigned int PrimerCluster, 
                                        __u64 Longitud, // Ignoramos este parámetro
                                        std::vector<unsigned> &Clusters)
{
    Clusters.clear(); //inicializar la lista en cero
    
    // los clusters 0 y 1 son especiales y no pueden ser parte de un archivo de usuario
    if (PrimerCluster < 2){return 0;} //error 

    TDatosFSFAT& fatData = this->DatosFS.DatosEspecificos.FAT;
    
    // 1. Apuntar al inicio de la PRIMERA FAT
    const unsigned char* pFAT = this->PunteroASector(fatData.SectoresReservados);
    if (pFAT == nullptr) return -1; // Error

    unsigned int clusterActual = PrimerCluster;
    
    // 2. Iterar hasta encontrar el marcador de Fin de Cadena (EOC)
    while (true)
    {
        // 3. Agregar el cluster actual a nuestra lista
        Clusters.push_back(clusterActual);

        unsigned int siguienteCluster = 0;

        // 4. Leer la siguiente entrada de la FAT12 

        // FAT12 usa entradas de 12 bits (1.5 bytes). Por ende, se levantan de memoria 3 bytes y se separa el cluster de interes por si es par o impar 
        // calculo donde arranca este cluster
        unsigned int offset = (clusterActual * 3) / 2; 
        
        // Leemos 16 bits (2 bytes) desde esa posición
        unsigned short valor16 = *( (const unsigned short*)(pFAT + offset) );
        
        //Separar por la paridad del cluster:: en Little Endian
        if (clusterActual % 2 == 0)
        {
            // Cluster PAR: tomar los 12 bits inferiores 
            siguienteCluster = valor16 & 0x0FFF;
        }
        else
        {
            // Cluster IMPAR: tomar los 12 bits superiores
            siguienteCluster = valor16 >> 4;
        }
        
        // EOC (End of Chain) para FAT12 
        if (siguienteCluster >= 0x0FF8) goto fin_de_cadena;
        
        // 5. IF NOT :: Avanzar al siguiente cluster
        clusterActual = siguienteCluster;
        
        // chequeo de nuevo:: los clusters 0 y 1 son especiales y no pueden ser parte de un archivo de usuario
        if (clusterActual < 2) goto fin_de_cadena;
    }

fin_de_cadena:
    return CODERROR_NINGUNO; 
}


/****************************************************************************************************************************************
 *																	*
 *						   TDriverFAT :: LevantarDatosSuperbloque						*
 *																	*
 * OBJETIVO: Esta función analiza el superbloque y completa la estructura DatosFS con los datos levantados.				*
 *																	*
 * ENTRADA: Nada.															*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores. Sino uno de los siguientes valores:				*
 *		CODERROR_SUPERBLOQUE_INVALIDO   : El superbloque está dañado o no corresponde a un disco con ningún formato.		*
 *		CODERROR_FILESYSTEM_DESCONOCIDO : El superbloque es válido, pero no corresponde a un FyleSystem soportado por esta	*
 *						  clase.										*
 *																	*
 ****************************************************************************************************************************************/

/* ================== Funciones a implementar por el alumno ================== */
int TDriverFAT::LevantarDatosSuperbloque()
{
    //Pedimos un puntero al inicio de la imagen del disco
    const unsigned char *sector0 = this->PunteroASector(0);
    //Chequear si fallo
    if (!sector0) return CODERROR_LECTURA_DISCO;

    //reinterpretar el puntero como si fuera nuestra estructura
    const TBiosParameterBlockFAT *bpb = reinterpret_cast<const TBiosParameterBlockFAT*>(sector0);
    const __u8 *sectorBuffer = reinterpret_cast<const __u8*>(sector0);

    //chequear que sea un bloque valido
    if (sectorBuffer[510] != 0x55 || sectorBuffer[511] != 0xAA)
        return CODERROR_SUPERBLOQUE_INVALIDO;
    //chequeo de seguridad :: fs corrupto o invalido
    if (bpb->BytesPorSector == 0 || bpb->SectoresPorCluster == 0 || 
        bpb->SectoresReservados == 0 || bpb->CopiasFAT == 0)
        return CODERROR_SUPERBLOQUE_INVALIDO;
    
    //atributos especificos de FAT
    TDatosFSFAT &fatData = this->DatosFS.DatosEspecificos.FAT;
    fatData.SectoresPorCluster = bpb->SectoresPorCluster; 
    fatData.SectoresReservados = bpb->SectoresReservados;
    fatData.CopiasFAT = bpb->CopiasFAT;
    fatData.EntradasRootDir = bpb->EntradasRootDir;
    fatData.SectoresPorFAT = bpb->SectoresPorFAT;
    fatData.SectoresOcultos = bpb->SectoresOcultos;
    
    // chequeo adicional, FAT12-16 tienen el root en un sector especial. c.c rechazo el FS
    if (bpb->EntradasRootDir == 0) return CODERROR_FILESYSTEM_DESCONOCIDO;

    this->DatosFS.BytesPorSector = bpb->BytesPorSector;
    //Calculo para numeros total de clusters
    __u32 totalSectoresDelDisco = (bpb->TotalSectores16 != 0) ? bpb->TotalSectores16 : bpb->TotalSectores32; // nuestro FS es FAT16, nunca se usa TotalSectores32
    __u32 bytesDelRootDir = bpb->EntradasRootDir * 32; //cada entrada del root ocupa 32 bytes
    // division entera haciendo ceiling
    __u32 sectoresRootDir = (bytesDelRootDir + bpb->BytesPorSector - 1) / bpb->BytesPorSector; //cuanto ocupa el root
    __u32 sectoresDeMetadata = bpb->SectoresReservados + (bpb->CopiasFAT * bpb->SectoresPorFAT) + sectoresRootDir ; // reservados + FATS + root
    __u32 totalSectoresDeDatos = totalSectoresDelDisco - sectoresDeMetadata; //el resto es sectores de usuario
    __u32 totalClusters = totalSectoresDelDisco / bpb->SectoresPorCluster;
    if (totalClusters >= 4085) return CODERROR_FILESYSTEM_DESCONOCIDO; //tester :: FAT12 solo puede mapear hasta 4085 sectores (2^12-1-totalSectoresDeDatos)
    fatData.TotalSectores = totalSectoresDelDisco;
    this->DatosFS.NumeroDeClusters = totalClusters;

    __u32 BytesPorCluster = this->DatosFS.BytesPorSector * fatData.SectoresPorCluster;
    this->DatosFS.BytesPorCluster = BytesPorCluster;
    // division entera haciendo ceiling
    fatData.ClustersRootDir = (bpb->EntradasRootDir * 32 + BytesPorCluster - 1) / BytesPorCluster;

    this->DatosFS.TipoFilesystem = static_cast<decltype(this->DatosFS.TipoFilesystem)>(1); //en mi enum, 1 es FAT12

    //devolver 0
    return CODERROR_NINGUNO;
}



/****************************************************************************************************************************************
 *																	*
 *						  TDriverFAT :: ListarDirectorio							*
 *																	*
 * OBJETIVO: Esta función enumera las entradas en un directorio y retorna un arreglo de elementos, uno por cada entrada.		*
 *																	*
 * ENTRADA: Path: Path al directorio enumerar (cadena de nombres de directorio separados por '/').					*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores, caso contrario el código de error.				*
 *	   Entradas: Arreglo con cada una de las entradas.										*
 *																	*
 ****************************************************************************************************************************************/


/**
 * ListarDirectorio (1) :: es un wrapper entre mi funcion propia de FAT y la interfaz del driver base.
 * Esta es la funcion heredada de driver_base.cpp 
 */

int TDriverFAT::ListarDirectorio(const char *Path, 
                                 std::vector<TEntradaDirectorio> &Entradas) //Entradas se pasa con referencia -> vector original
{
    std::vector<unsigned int> clustersRoot; //inicializar una cadena vacia de clusters
    
    // 1. Limpiar el path del caracter '/'
    const char* pathSinRoot = Path;
    if (pathSinRoot[0] == '/'){pathSinRoot++;}

    // 2. Obtener los clusters del Directorio Raíz (en FAT12 es cero)
    clustersRoot.push_back(0); 
    
    // 3. Iniciar la llamada a ListarDirectorio (2):: navegadora
    return this->ListarDirectorio(clustersRoot, pathSinRoot, Entradas);
}

/**
/*Funcion ListarDirectorio (2):: navegadora
*/

int TDriverFAT::ListarDirectorio(std::vector<unsigned int> &ClustersDirActual, const char *SubDirs, std::vector<TEntradaDirectorio> &Entradas) 
{
    // 1. Partir el path actual 
    std::string componenteActual;
    std::string restoDelPath;
    PopPathComponent(SubDirs, componenteActual, restoDelPath);

    // 2. Obtener las entradas del directorio donde estamos parados
    std::vector<TEntradaDirectorio> entradasActuales; //inicializo un estructura nueva de TEntradaDirectorio para listar mi directorio actual y los subsiguientes

    // 2b. Llamo a mi funcion ListarDirectorio (3):: lectora. Le paso mi vector de entradas
    int err = this->ListarDirectorio(ClustersDirActual, entradasActuales);
    if (err != 0){return err;} // Error al leer el directorio

    // 3. CASO BASE: chequeamos la condicion del path; si esta vacio, no queda mas recursividad
    if (componenteActual.empty())
    {
        // Llegamos al directorio final. Guardamos los resultados en Entradas y salimos del bucle
        Entradas = entradasActuales;
        return CODERROR_NINGUNO; // CODERROR_NINGUNO
    }

    // 4. CASO RECURSIVO: Buscar el siguiente subdirectorio
    for (const TEntradaDirectorio& entrada : entradasActuales)
    {
        // Comparar el nombre de la entrada con el componente del path
        if (entrada.Nombre == componenteActual.c_str())
        {
            // ¡Lo encontramos!
            
            // 4a. Verificar que SEA un directorio
            // Usamos el flag genérico de driver_base.h
            if (!(entrada.Flags & fedDIRECTORIO))
            {
                return -1; // Error: Se intentó listar un archivo
            }
            
            // 4b. Obtener la cadena de clusters de este subdirectorio
            // Usamos el cluster guardado en la unión
            unsigned int primerCluster = entrada.DatosEspecificos.FAT.PrimerCluster;
            std::vector<unsigned int> clustersSiguienteDir;
            
            err = this->BuscarCadenaDeClusters(primerCluster, 0, clustersSiguienteDir);
            if (err != 0)
            {
                return err; // Error leyendo la FAT
            }

            // 4c. Llamada recursiva con el resto del path
            return this->ListarDirectorio(clustersSiguienteDir, restoDelPath.c_str(), Entradas);
        }
    }

    // 5. Si salimos del 'for', es porque no encontramos el componente
    return -1; // CODERROR_PATH_NO_ENCONTRADO
}


/**
/*Funcion ListarDirectorio (3):: lectora
*/
int TDriverFAT::ListarDirectorio(std::vector<unsigned int> &Clusters, std::vector<TEntradaDirectorio> &Entradas) { 
    //vaciar todas las entradas que habia hasta ahora: me interesa listar solo el DIR que me pasaron por path
    Entradas.clear();
    
    TDirEntryFAT* rawEntry; // La struct cruda de driver_fat.h
    TEntradaDirectorio nuevaEntrada; // La struct limpia de driver_base.h (generica)
    
    // un alias para los atributos de FAT
    TDatosFSFAT& fatData = this->DatosFS.DatosEspecificos.FAT;

    // --- CASO 1: leer la raiz para FAT12  ---> el root esta marcado con el cluster cero
    if (Clusters.size() == 1 && Clusters[0] == 0)
    {
        // calculamos offset :: FAT12 tiene primero el sectores reservados (SPB + Reservados) + FATS 
        unsigned int offsetRootDirSectores = fatData.SectoresReservados + (fatData.CopiasFAT * fatData.SectoresPorFAT);
        const unsigned char* pBufferRoot = this->PunteroASector(offsetRootDirSectores);
        
        // iteramos por el número fijo de entradas
        for (int i = 0; i < fatData.EntradasRootDir; i++)
        {
            // casteamos el puntero a nuestra struct cruda TDirEntryFAT
            rawEntry = (TDirEntryFAT*)(pBufferRoot + (i * 32));
            
            // 0x00 = fin del directorio
            if (rawEntry->Name[0] == 0x00) break;
            
            // 0xE5 = entrada borrada 
            if ((unsigned char)rawEntry->Name[0] == 0xE5) continue;
            
            // si la entrada del directorio es valida, la parseamos con nuestra func auxiliar
            if (this->ParsearEntradaFAT(rawEntry, nuevaEntrada))
            {
                Entradas.push_back(nuevaEntrada);
            }
        }
    }
    // --- CASO 2: es un subdirectorio  ---
    else
    {
        unsigned int entradasPorCluster = this->DatosFS.BytesPorCluster / 32; //cada entrada ==> 32 bytes fijos

        for (unsigned int numCluster : Clusters)
        {
            if (numCluster < 2) continue; // Clusters 0 y 1 son reservados
            
            const unsigned char* pBufferCluster = this->PunteroACluster(numCluster);
            if (pBufferCluster == nullptr) return -1; // Error

            for (unsigned int i = 0; i < entradasPorCluster; i++)
            {
                // Casteamos el puntero a nuestra struct cruda TDirEntryFAT
                rawEntry = (TDirEntryFAT*)(pBufferCluster + (i * 32));

                // 0x00 = Fin del directorio
                if (rawEntry->Name[0] == 0x00)
                {
                    goto fin_del_directorio; // No hay más entradas en ningún cluster
                }

                // 0xE5 = Entrada borrada
                if ((unsigned char)rawEntry->Name[0] == 0xE5) continue;

                // Usamos nuestra función "traductora"
                if (this->ParsearEntradaFAT(rawEntry, nuevaEntrada))
                {
                    Entradas.push_back(nuevaEntrada);
                }
            }
        }
    }

fin_del_directorio:
    return CODERROR_NINGUNO;
}















/****************************************************************************************************************************************
 *																	*
 *						     TDriverFAT :: LeerArchivo								*
 *																	*
 * OBJETIVO: Esta función levanta de la imágen un archivo dada su ruta.									*
 *																	*
 * ENTRADA: Path: Ruta al archivo a levantar.												*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores, caso contrario el código de error.				*
 *	   Data: Buffer alocado con malloc() con los datos del archivo.									*
 *	   DataLen: Tamaño en bytes del buffer devuelto.										*
 *																	*
 * OBSERVACIONES: Los valores Data y DataLen sólo devuelven valores válidos si se retorna CODERROR_NINGUNO.				*
 *																	*
 ****************************************************************************************************************************************/
int TDriverFAT::LeerArchivo(const char *Path, unsigned char *&Data, unsigned &DataLen)
{
        // Inicializar salidas
    Data = nullptr;
    DataLen = 0;

    // 1) Listar el directorio que contiene el archivo
    std::vector<TEntradaDirectorio> entradas;

    // Extraer nombre de archivo y directorio padre
    std::string sPath(Path);
    size_t pos = sPath.find_last_of('/');
    std::string nombreArchivo = (pos == std::string::npos) ? sPath : sPath.substr(pos + 1);
    std::string dirPath;

    // Manejar casos especiales
    if (pos == std::string::npos)
        dirPath = ".";
    else if (pos == 0)
        dirPath = "/";
    else
        dirPath = sPath.substr(0, pos);

    // 2) Listar el directorio padre
    int err = this->ListarDirectorio(dirPath.c_str(), entradas);
    if (err != 0)
    {
        // Intentar listar el directorio completo (caso raíz)
        int err2 = this->ListarDirectorio(Path, entradas);
        if (err2 != 0) return err2;
        err = err2;
    }

    // 3) Buscar la entrada correspondiente
    auto trim = [](const std::string &s)->std::string {
        size_t a = 0, b = s.size();
        while (a < b && s[a] == ' ') ++a;
        while (b > a && s[b-1] == ' ') --b;
        return s.substr(a, b - a);
    };
    auto lower_inplace = [](std::string &s){ for (size_t i = 0; i < s.size(); ++i) s[i] = tolower((unsigned char)s[i]); };

    for (const TEntradaDirectorio& entrada : entradas)
    {
        std::string rawNombre = entrada.Nombre.c_str();
        std::string rawBuscado = nombreArchivo;

        std::string nombreEntrada = trim(rawNombre);
        std::string buscado = trim(rawBuscado);
        lower_inplace(nombreEntrada);
        lower_inplace(buscado);

        printf("Nombre Entrada: %s, Buscado: %s, normalizados: '%s' vs '%s'\n", rawNombre.c_str(), rawBuscado.c_str(), nombreEntrada.c_str(), buscado.c_str());

        //Si coincide el nombre de la entrada con el buscado, procedemos a leer el archivo
        if (nombreEntrada == buscado)
        {

            // 4) Obtener cadena de clusters del archivo
            std::vector<unsigned int> clustersArchivo;

            //Entregar el primer cluster del archivo
            unsigned int primerCluster = entrada.DatosEspecificos.FAT.PrimerCluster;

            err = this->BuscarCadenaDeClusters(primerCluster, entrada.Bytes, clustersArchivo);
            if (err != 0)
            {
                return err; // Propagar error (p. ej. lectura FAT)
            }

            // 5) Leer los datos del archivo cluster por cluster
            unsigned int bytesLeidos = 0;
            //Calculo los bytes por cluster -> Muchos Datos Previamente Calculados en LevantarDatosSuperbloque
            unsigned int bytesPorCluster = this->DatosFS.BytesPorCluster;
            //Calculo el total de bytes del archivo
            unsigned int totalBytesArchivo = entrada.Bytes;
            //Calculo el total de clusters a ocupar, es decir cuantos clusters necesito para leer todo el archivo
            unsigned int TotalDeClustersAOcupar = (totalBytesArchivo + bytesPorCluster - 1) / bytesPorCluster; // ceil

            // Reservamos buffer de salida directamente en ⁠ Data ⁠
            // El tamaño es el tam del archivo, es decir totalBytesArchivo
            DataLen = totalBytesArchivo;
    
            //Aloco memoria para Data
            Data = (unsigned char*) malloc(DataLen);
            if (!Data)
            {
                DataLen = 0;
                return CODERROR_FALTA_MEMORIA;
            }
            //Creo una lista con los Clusters del Archivo:
            std::vector<unsigned int> ClustersDelArchivo;

            //Hago un for para llenar el vector con los clusters del archivo
            //Se puede optimizar con funciones ya hechas -> No se me ocurrio como
            for (unsigned int i = 0; i < TotalDeClustersAOcupar; i++)
            {
                // Asigno el primer cluster del archivo
                int clusterDondeArranca = primerCluster;
                //Guardo en el vector el cluster del archivo todos los clusters que ocupa a partir del primer cluster
                ClustersDelArchivo.push_back(clusterDondeArranca + i);
            }

            // Copiar cluster por cluster hacia Data usando offset
            unsigned int offset = 0;
            for (unsigned int cluster : ClustersDelArchivo)
            {
                //saco la data del cluster en donde esta el archivo
                //Esto lo hago por cada cluster que tiene el archivo
                const unsigned char* pClusterData = this->PunteroACluster(cluster);

                //Calculo cuantos bytes quedan por copiar, es decir los bytes que quedan por leer del total del archivo
                unsigned int bytesRestantes = (DataLen > offset) ? (DataLen - offset) : 0;
                if (bytesRestantes == 0) break;

                //Calculo cuantos bytes copiar
                unsigned int bytesACopiar = (bytesRestantes < bytesPorCluster) ? bytesRestantes : bytesPorCluster;

                //Voy copiando los datos al buffer Data, cluster por cluster
                memcpy(Data + offset, pClusterData, bytesACopiar);
                offset += bytesACopiar;
            }


            return CODERROR_NINGUNO;
        }
    }
    

    // Si no encontró el archivo dentro de las entradas
    return CODERROR_ARCHIVO_INEXISTENTE; // o CODERROR_ARCHIVO_NO_ENCONTRADO

}
