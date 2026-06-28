1. Currently we are unable to connect via the REMOTE server.  AdsConnect60() should be working with REMOTE setting.  Find why DAWeb cannot connect via remote.

2. User AdsSys is being created on target directory after importing from SAP DD.  Below is the message shown after importing:
✓ Import complete
  Group memberships: 25
  Permissions:       559
  DB properties:     12
  Registered as "PMSys_OpenADS" in the connection list

Warnings:
  • DB prop sap_id=114 rc=5138
  • DB prop sap_id=115 rc=5138
  • DB prop sap_id=116 rc=5138
  • DB prop sap_id=117 rc=5138
  • DB prop sap_id=118 rc=5138
  • DB prop sap_id=3 rc=5138
  • adssys created (SAP built-in, not in export)
  • add_user_to_group(adssys,DB:Admin): AdsSys group membership cannot be changed

  Notice ti creates adssys user but it is not being able to add to group db:admin.  In fact the user is created and it shows as belonging to db:public but does not show belonging to db:admin.  However, loging as adssys, I notice I'm able to open all tables, view all DD properties using daweb.

  If effective permmissions for just created user adssys is zero, then being able to select and view tables as well as other dd properties and objects, should not be allowed.

  I need you to check both.  #1-- we do need adssys to belong to db:admin, #2-- if a user indeed does not have permissions to select and view a table or other dd properties, then openads core needs to enforce it.