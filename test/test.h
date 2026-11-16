//
// Created by arisu on 10/11/2026.
//

#ifndef KWDB_TEST_H
#define KWDB_TEST_H

#pragma once
void testSegmentRotation();
void testSegmentRotationWithCheckpoint();
void testLastSegmentTruncatedInsideHeader();
void testMiddleSegmentTruncatedHeaderThrows();
void testMutualExclusion();
void testConcurrentMixedOperations();

#endif //KWDB_TEST_H