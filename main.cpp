#include <iostream>
#include "database/database.h"

int main() {
    Database db = Database();
    db.put("name", "Aris");
    db.put("city", "Como");
    db.remove("city");
}