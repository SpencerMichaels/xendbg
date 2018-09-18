//
// Created by Spencer Michaels on 8/13/18.
//

#include <cstring>
#include <iostream>

#include "Domain.hpp"
#include "XenForeignMemory.hpp"
#include "XenException.hpp"

// NOTE: This needs to be declared after Domain.hpp, which includes xenctrl.h.
// For some reason, including xenforeignmemory.h before xenctrl.h will fail.
#include "BridgeHeaders/xenforeignmemory.h"

using xd::xen::MappedMemory;
using xd::xen::WordSize;
using xd::xen::XenForeignMemory;
using xd::xen::XenException;

XenForeignMemory::XenForeignMemory()
    : _xen_foreign_memory(xenforeignmemory_open(NULL, 0), xenforeignmemory_close)
{
  if (!_xen_foreign_memory)
    throw XenException("Failed to open Xen foreign memory handle!", errno);
}

/*
 * NOTE: the p2m table doesn't seem to contain a mapping for the null page.
 */
MappedMemory XenForeignMemory::map(const Domain &domain, Address address, size_t size, int prot) const {
  xen_pfn_t base_mfn = pfn_to_mfn_pv(domain, address >> XC_PAGE_SHIFT);
  size_t num_pages = (size + XC_PAGE_SIZE - 1) >> XC_PAGE_SHIFT;

  auto pages = (xen_pfn_t*)malloc(num_pages * sizeof(xen_pfn_t));
  auto errors = (int*)malloc(num_pages * sizeof(int));

  if (!pages)
    throw XenException("Failed to allocate PFN table: ", errno);
  if (!errors)
    throw XenException("Failed to allocate error table: ", errno);

  for (size_t i = 0; i < num_pages; ++i) {
    pages[i] = base_mfn + i;
  }

  unsigned char *mem_page_base = (unsigned char*)xenforeignmemory_map(_xen_foreign_memory.get(),
      domain.get_domid(), prot, num_pages, pages, errors);
  unsigned char *mem = mem_page_base + address % XC_PAGE_SIZE;

  for (size_t i = 0; i < num_pages; ++i) {
    if (errors[i])
      throw XenException("Failed to map page " + std::to_string(i+1) + " of " +
          std::to_string(num_pages), -errors[i]);
  }

  auto fmem = _xen_foreign_memory;
  return std::shared_ptr<unsigned char>(mem, [fmem, address, num_pages](void *memory) {
    if (memory) {
      xenforeignmemory_unmap(fmem.get(), (void*)address, num_pages);
    }
  });
}

// See xen/tools/libxc/xc_offline_page.c:389
xen_pfn_t XenForeignMemory::pfn_to_mfn_pv(const Domain &domain, xen_pfn_t pfn) {
  const auto meminfo = domain.map_meminfo();
  const auto word_size = domain.get_word_size();

  if (pfn > meminfo->p2m_size)
    throw XenException("Invalid PFN!");

  if (word_size == sizeof(uint64_t)) {
    return ((uint64_t*)meminfo->p2m_table)[pfn];
  } else {
    uint32_t mfn = ((uint32_t*)meminfo->p2m_table)[pfn];
    return (mfn == ~0U) ? INVALID_MFN : mfn;
  }
}