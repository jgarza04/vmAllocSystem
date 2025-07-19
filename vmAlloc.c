#include "cpen212vm.h"
#include <string.h>

typedef struct freePageNode{
    struct freePageNode* next;
}freePageNode;

typedef struct {
    void*  vmStart;
    void* vmEnd;
    void* firstPage;
    uint32_t numPages;
    FILE* swapFile;
    uint32_t numSwapPages;
    paddr_t asid[512]; // Stores the actual address in which the pt starts
    freePageNode* freePageHeader; // The address of the header of the free page free list.
} metadata;



// description:
// - initializes a VM system
// arguments:
// - physmem: pointer to an area of at least 4096 * num_phys_pages bytes that models the physical memory
//   - no vm_* functions may access memory outside of [physmem, physmem+4096*num_phys_pages)
//   - all physical addresses are offsets from physmem (i.e., physical address is exactly 0 at physmem)
// - num_phys_pages: total number of 4096-byte physical pages available
//   - it is guaranteed that 4 <= num_phys_pages <= 1048576
//   - physical page 0 starts at physmem
// - swap
//   - if non-null: pointer to a swap file opened in read-write mode with size 4096 * num_swap_pages bytes
//   - if null: no swap space is available for this VM instance
// - num_swap_pages: total number of 4096-byte pages available in the swap file
//   - only relevant if swap is not null
//   - if swap is non-null, it is guaranteed that 2 <= num_swap_pages <= 67108864
// returns:
// - on success, a non-NULL handle that uniquely identifies this VM instance;
//   this will be passed unchanged to other vm_* functions
// - on failure (I/O error), NULL
void *vm_init(void *physmem, size_t num_phys_pages, FILE *swap, size_t num_swap_pages) {
    // YOUR CODE HERE
    
    if(physmem == NULL)	return NULL;
    if(!num_phys_pages) return NULL;
    metadata* metaData = (metadata*)physmem;
    memset(metaData, 0, sizeof(metadata));

    metaData->vmStart = physmem;
    metaData->vmEnd = (void*)((uintptr_t)physmem + 4096*num_phys_pages);
    metaData->firstPage = (void*)((uintptr_t)physmem + 4096);
    metaData->numPages = num_phys_pages - 1;
    metaData->swapFile = NULL;
    metaData->numSwapPages = num_swap_pages;
    metaData->freePageHeader = (freePageNode*)((uintptr_t)physmem + 4096);
    
    for(uint32_t i = 0; i < 512; i+=4096) {
        metaData->asid[i] = 0;
    }

    freePageNode* page = metaData->freePageHeader;

    for (size_t i = 1; i < num_phys_pages - 1; i++) {
        page->next = (freePageNode*)((uintptr_t)page + 4096);
        page = page->next;
    }
    page->next = NULL;
    return physmem;
}

// description: 
// - gets the first level page offset from the virtual address if possible
// arguments:
// - addr: the virtual address that is to be translated
// returns:
// - a uint32_t value that represents the first level page offset.
static uint32_t getFirstLevel(vaddr_t addr){
	if(!addr) return 0;
	return (addr >> 22) & 0x3FF;
}

// description:
// - gets the second level page offset from the virtual address if possible
// arguments:
// - addr: the virtual address that is to be translated
// returns:
// - a uint32_t value that represents the second level page offset.
static uint32_t getSecondLevel(vaddr_t addr){
    if(!addr) return 0;
	return (addr >> 12) & 0x3FF;
}

// description:
// - gets the page offset from the virtual address if possible
// arguments:
// - addr: the virtual address that is to be translated
// returns:
// - a uint32_t value that represents the page offset.
static uint32_t getOffset(vaddr_t addr){
        if(!addr) return 0;
        return (addr & 0xFFF);
}

// description:
// - translates a virtual address to a physical address if possible
// arguments:
// - vm: a VM system handle returned from vm_init
// - pt: physical address of the top-level page table of the address space being accessed
// - addr: the virtual address to translate
// - access: the access being made (instruction fetch, read, or write)
// - user: the access is a user-level access (i.e., not a kernel access)
// input invariants:
// - pt was previously returned by vm_new_addr_space()
// returns:
// - the success status of the translation:
//   - VM_OK if translation succeeded
//   - VM_BAD_ADDR if there is no translation for this address
//   - VM_BAD_PERM if permissions are not sufficient for the type / source of access requested
//   - VM_BAD_IO if accessing the swap file failed
// - the resulting physical address (relevant only if status is VM_OK)
vm_result_t vm_translate(void *vm, paddr_t pt, vaddr_t addr, access_type_t access, bool user) {
    // YOUR CODE HERE
    uint32_t firstLevelPage = getFirstLevel(addr);
    uint32_t secondLevelPage = getSecondLevel(addr);
    uint32_t pageOffset = getOffset(addr);
    
    vm_result_t translationResult;
    // Must look into first level page table and seek entry
    uint32_t * firstLevelEntry = (uint32_t*)(pt + firstLevelPage*4 + ((metadata*)vm)->vmStart);

    // Check if first level page entry is valid
    if(!(*(firstLevelEntry) & 0b1)){
	    translationResult.status = VM_BAD_ADDR;
	    translationResult.addr = 0;
	    return translationResult;
    }

    // Look into second level page table and seek entry
    uint32_t * secondLevelEntry = (uint32_t*)(((metadata*)vm)->vmStart + (*firstLevelEntry & 0xFFFFF000) + secondLevelPage*4);

   // Check if second level page entry is valid
   if(!(*secondLevelEntry & 0b1)){ 
        translationResult.status = VM_BAD_ADDR;
        translationResult.addr = 0;
        return translationResult;
   }

   // Check if the user can access the data
   if(user){
	   if(!(*secondLevelEntry & 0b100000)){
		   translationResult.status = VM_BAD_PERM;
		   translationResult.addr = 0;
		   return translationResult;
	   }
   }

   // Check if specific access type is allowed
   if(access == VM_EXEC){
	   if(!(*secondLevelEntry & 0b10000)){
		translationResult.status = VM_BAD_PERM;
		translationResult.addr = 0;
		return translationResult;
	   }
   }else if(access == VM_READ){
           if(!(*secondLevelEntry & 0b100)){
                translationResult.status = VM_BAD_PERM;
                translationResult.addr = 0;
		return translationResult;
           }
   }else if(access == VM_WRITE){
           if(!(*secondLevelEntry & 0b1000)){
                translationResult.status = VM_BAD_PERM;
                translationResult.addr = 0;
                return translationResult;
           }
   }

   // Return the mapping from the second table entry
   translationResult.status = VM_OK;
   translationResult.addr = (*secondLevelEntry & 0xFFFFF000) + pageOffset;
   return translationResult;

}

// Will add a node to the linked list of free pages. 
// Does so by adding it to the header of the list.
static void addFreeList(void *vm, void* address){
    metadata* metaData = (metadata*)vm;
    freePageNode* newPage = (freePageNode*)address;
    newPage->next = metaData->freePageHeader;
    metaData->freePageHeader = newPage;
}

// Will remove a node from the linked list of free pages.
// Does so by pointer before taking the node's pointer.
static void removeFreeList(void *vm, freePageNode* address){
    metadata* metaData = (metadata*)vm;
    freePageNode* pageToRemove = address;

    //Check if linked list is not empty
    if (!metaData || metaData->freePageHeader == NULL) return;

    // Check if the node is the header of the free list.
    if(address == metaData->freePageHeader){
        metaData->freePageHeader = pageToRemove->next;
        *((uintptr_t*)pageToRemove) = 0;
        return;
    }
    freePageNode* page = metaData->freePageHeader;

    for(freePageNode* page = metaData->freePageHeader; page != NULL; page = page->next){
        if(page->next == address){
            page->next = pageToRemove->next;
            *((uintptr_t*)pageToRemove) = 0;
            return;
        }
    }
}


// description:
// - adds a top-level page table for an address space
// arguments:
// - vm: a VM system handle returned from vm_init
// - asid: address space ID of for which the toplevel page table should be created
// returns:
// - the success status:
//   - VM_OK if a new page table was created
//   - VM_OUT_OF_MEM if no free pages remain in the physical memory and any relevant swap
//   - VM_BAD_IO if accessing the swap file failed
// - the physical address of the *top-level* page table for this address space (relevant only if status is VM_OK)
// input invariants:
// - 0 <= asid < 512
// - asid is not currently active
// output invariants:
// - a toplevel page table for address space asid exists in physical memory
vm_result_t vm_new_addr_space(void *vm, asid_t asid) {
    // YOUR CODE HERE
    metadata* metaData = (metadata*)vm;
    vm_result_t result;

    // Check if ASID already has an L1 table
    if(!metaData || metaData->asid[asid] != 0) {
        return (vm_result_t){ .status = VM_DUPLICATE };
    }

    // Check if there is a free page available
    if(!metaData || metaData->freePageHeader == NULL) return (vm_result_t){ .status = VM_OUT_OF_MEM };

    // Allocate the header of the freelist and remove it from the free list.
    freePageNode* freePage = metaData->freePageHeader;
    removeFreeList(vm, freePage);
    
    // Set the freePage to be one of the top level page tables.
    metaData->asid[asid] = (uintptr_t)freePage - (uintptr_t)metaData->vmStart;

    // Zero out the new L1 table (ensure all entries are initially invalid)
    memset((void*)freePage, 0, 4096);
    result.status = VM_OK;
    result.addr = (uintptr_t)freePage - (uintptr_t)metaData->vmStart;
    return result;
}



// description:
// - entirely removes an address space
// arguments:
// - vm: a VM system handle returned from vm_init
// - asid: the ID of the address space to be removed
// returns:
// - the success status:
//   - VM_OK if the address space was successfully removed
//   - VM_BAD_ADDR if the toplevel page table for this address pace does not exist
//   - VM_BAD_IO if accessing the swap file failed
// input invariants:
// - 0 <= asid < 512
// - asid is currently active
// output invariants:
// - all pages and page tables used by address space asid are no longer allocated in physical memory or swap
vm_status_t vm_destroy_addr_space(void *vm, asid_t asid) {
    // YOUR CODE HERE
    metadata* metaData = (metadata*)vm;

    // Check if the address space has a top level page table
    if(metaData->asid[asid] == 0) return VM_BAD_ADDR;

    // Get pointer to the top level page table
    void* topLevelPage = (void*)(metaData->asid[asid] + (uintptr_t)vm);

    if((uintptr_t)topLevelPage % 4096 != 0){ 
        printf("Top level not aligned.\n");
        return VM_BAD_IO;
    }

    // Loop through the top level page, if page is mapped (bit is valid) iterate through second
    // level page freeing all of the physical pages it maps to
    for(int i = 0; i < 4096; i+=4){
        uint32_t* firstLevelEntry = (uint32_t*)((uintptr_t)topLevelPage + i);
        // Check if entry is valid
        if(*firstLevelEntry & 0b1){
            
            void* secondLevelPage = (void*)((uintptr_t)vm + (*firstLevelEntry & 0xFFFFF000));
            if((uintptr_t)secondLevelPage % 4096 != 0){ 
                printf("Top level not aligned.\n");
                return VM_BAD_IO;
            }
            // Iterate over second level page 
            for(int i = 0; i < 4096; i+=4){
                uint32_t* secondLevelEntry = (uint32_t*)((uintptr_t)secondLevelPage + i);
                
                //Check if entry is valid
                if(*secondLevelEntry & 0b1){
                    // Free physical page
                    void* physicalPage = (void*)((*secondLevelEntry & 0xFFFFF000) + (uintptr_t)vm);
                    memset(physicalPage, 0, 4096);
                    addFreeList(vm, physicalPage);
                }
            }
            addFreeList(vm, secondLevelPage);
        }
    }
    addFreeList(vm, topLevelPage);
    metaData->asid[asid] = 0;
    return VM_OK;
}

// description:
// - creates a mapping for a new page in the virtual address space and map it to a physical page
// arguments:
// - vm: a VM system handle returned from vm_init
// - pt: physical address of the top-level page table
// - addr: the virtual address on a page that is to be mapped (not necessarily the start of the page)
// - user: the page is accessible from user-level processes
// - exec: instructions may be fetched from this page
// - write: data may be written to this page
// - read: data may be read from this page
// returns:
// - the success status of the mapping:
//   - VM_OK if the mapping succeeded
//   - VM_OUT_OF_MEM if no free pages remain in the physical memory and any relevant swap
//   - VM_DUPLICATE if a mapping for this page already exists
//   - VM_BAD_IO if accessing the swap file failed
// input invariants:
// - pt was previously returned by vm_new_addr_space()
vm_status_t vm_map_page(void *vm, paddr_t pt, vaddr_t addr, bool user, bool exec, bool write, bool read) {
    // YOUR CODE HERE
    metadata* metaData = (metadata*)vm;
    uint32_t firstLevelIndex = getFirstLevel(addr);
    uint32_t secondLevelIndex = getSecondLevel(addr);
    uint32_t pageOffset = getOffset(addr);

    uint32_t * firstLevelEntry = (uint32_t*)(pt + firstLevelIndex*4 + (uintptr_t)metaData->vmStart);

    // Check if first level page entry is valid, if not we must allocate second level page
    if(!(*(firstLevelEntry) & 0b1)){
        // Since L1 page entry is not valid, allocate L2 page.
        
        // If not free pages return out of mem
        if (metaData->freePageHeader == 0) return VM_OUT_OF_MEM;

        // Allocate second level page
        freePageNode* newSecondLevelPage = metaData->freePageHeader;
        removeFreeList(vm, newSecondLevelPage);

        // Clear the second level page
        memset((void*)newSecondLevelPage, 0, 4096);

        // Put the second level page in first level entry with valid bit set
        *firstLevelEntry = ((((uintptr_t)newSecondLevelPage - (uintptr_t)vm) << 12) & 0xFFFFF000) | 0b11;
    }
    
    // Look into second level page table and seek entry
    uint32_t * secondLevelEntry = (uint32_t*)(((metadata*)vm)->vmStart + ((*firstLevelEntry >> 12)) + secondLevelIndex*4);
    
    // Check if the second level page entry is valid, if yes, return VM_DUPLICATE
    if(*secondLevelEntry & 0b1) return VM_DUPLICATE;
    
    // We now need to allocate a new physical page
    // Check if their is an available page, if not return VM_OUT_OF_MEM
    if (metaData->freePageHeader == NULL) return VM_OUT_OF_MEM;
    
    // Allocate a page
    freePageNode* newPhysicalPage = metaData->freePageHeader;
    removeFreeList(vm, newPhysicalPage);

    // Create the page table entry and set the permission bits
    uint32_t pageTableEntry = ((uintptr_t)newPhysicalPage & 0xFFFFF000) | 0b11;
    if(user) pageTableEntry = pageTableEntry | 0b100000;
    if(exec) pageTableEntry = pageTableEntry | 0b10000;
    if(write) pageTableEntry = pageTableEntry | 0b1000;
    if(read) pageTableEntry = pageTableEntry | 0b100;

    // Put the page table entry into the second level entry
    *secondLevelEntry = pageTableEntry;

    return VM_OK;
}

// description:
// - removes the mapping for the page that contains virtual address addr
// - returns any unmapped pages and any page tables with no mappings to the free page pool
// arguments:
// - vm: a VM system handle returned from vm_init
// - pt: physical address of the top-level page table of the address space
// - addr: the virtual address on a page that is to be unmapped (not necessarily the start of the page)
// returns:
// - the success status of the unmapping:
//   - VM_OK if the page was successfully unmapped
//   - VM_BAD_ADDR if this address space has no mapping for virtual address addr
//   - VM_BAD_IO if accessing the swap file failed

vm_status_t vm_unmap_page(void *vm, paddr_t pt, vaddr_t addr) {
    // YOUR CODE HERE
    metadata* metaData = (metadata*)vm;
    // Get the specific bit sections
    uint32_t firstLevel = getFirstLevel(addr);
    uint32_t secondLevel = getSecondLevel(addr);
    uint32_t offset = getOffset(addr);

    // Get the first level entry
    uint32_t* firstLevelEntry = (uint32_t*)((uintptr_t)vm + pt + firstLevel*4);

    // Check if first level entry is valid, if not return VM_BAD_ADDR
    if(!(*firstLevelEntry & 0b1)) return VM_BAD_ADDR;

    // Get the second level page
    void* secondLevelPage = (uintptr_t)vm + (*firstLevelEntry & 0xFFFFF000);

    // Get the second level entry
    uint32_t* secondLevelEntry = (uint32_t*)((uintptr_t)secondLevelPage + secondLevel);

    // Check if the second level entry is valid, if not return VM_BAD_ADDR
    if(!(*secondLevelEntry & 0b1)) return VM_BAD_ADDR;

    // Get the physical page
    void* physicalPage = (uintptr_t)vm + (*secondLevelEntry & 0xFFFFF000);

    // Free the physical page and add it to the free list
    memset(physicalPage, 0, 4096);
    addFreeList(vm, physicalPage);

    // Set the second level entry to 0
    *secondLevelEntry = 0;

    // Iterate over second level page to see if there is an active page
    for(int i = 0; i < 4096; i+=4){
        uint32_t* secondLevelEntryIterate = (uint32_t*)((uintptr_t)vm +(uintptr_t)secondLevelPage + i);
        if(*secondLevelEntryIterate & 0b1){
            return VM_OK;
        }
    }

    // Free second level page
    memset(secondLevelPage, 0, 4096);
    addFreeList(vm, secondLevelPage);

    // Iterate over first level page to see if there is an active page
    for(int i = 0; i < 4096; i+=4){
        uint32_t* firstLevelEntryIterate = (uint32_t*)(pt + (uintptr_t)vm + i);
        if(*firstLevelEntryIterate & 0b1){
            return VM_OK;
        }
    }

    // Free first level page
    memset((void*)(pt+(uintptr_t)vm), 0, 4096);
    addFreeList(vm, (void*)(pt+(uintptr_t)vm));

    return VM_OK;
}
