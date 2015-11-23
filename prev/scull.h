#ifndef SCULL_H_
#define SCULL_H_

struct scull_qset {
  void** data;
  struct scull_qset* next;
};

#endif  // SCULL_H_
