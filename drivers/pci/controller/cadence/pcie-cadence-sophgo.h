#ifndef PCIE_CADENCE_SOPHGO
#define PCIE_CADENCE_SOPHGO


typedef struct {
	const char  *name;
	uint16_t    vendor_id;
} vendor_id_list_t;

extern vendor_id_list_t vendor_id_list[];
extern size_t vendor_id_list_num;

extern struct irq_domain *cdns_pcie_get_parent_irq_domain(int intc_id);
int check_vendor_id(struct pci_dev *dev, vendor_id_list_t vendor_id_list[], size_t vendor_id_list_num);
#endif
