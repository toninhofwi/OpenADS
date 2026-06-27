1. connecting to a Data Dictionary, user names needs to be case-insensitve.  I changed src/engine/data_dict.h: has_user is no longer inline — it now delegates to .cpp so it can call ci_name. Added static std::string ci_name(const std::string&) noexcept declaration.

src/engine/data_dict.cpp: Added ci_name (lowercases with tolower per-char). Applied ci_name(user) in every method that takes a user name: has_user, create_user, delete_user, add_user_to_group, remove_user_from_group, is_member_of, groups_of, set_user_property, get_user_property, build_perm_cache, check_perm, get_effective_ops, get_all_effective_perms, get_effective_permission. Also applied at all three load-time insertion points: the binary .add User record loader, the binary permission/membership record loader, the third-pass DB: cipher decoder, and the JSON loader for User and Member object types.

src/session/connection.h: set_username now lowercases before storing, so conn->username() always returns a lowercase name — this covers all the c->username() calls in ace_exports.cpp (effective-permission checks, login check) without needing to touch that file.

write comments about why these changes on source.  Start comments with my initials RCB followed by date.
You've hit your weekly limit

2. on DAWeb (\OpenAds\DA-Web\) when prompting for username and password dialog, we need to have tags or a toggle control where the user can click to choose between LOCAL or Remote server and connection needs to happen in either remote server or local server modes.

3. User permissions is not showing information correctly on imported DD. Nor is it saving correctly.  When turning on Execute permission to group Administrators and clicking on save, it saves nothing.   Source DD is c:\Pmsys\data\pmsys.add imported DD is c:\Pmsys\data\pmsys_OpenADS.add.

On imported DD, user RCB is a member of db:Admin and also of group Administrators.  Tab with direct permissions shows correct information, tab with effective permissions should show all permissions assigned to the groups the user belongs to.  In this case it shows no permissions.  The user is a member of db:Admin and also of group Administrators.  I think that if I remove all groups and only leave db:Admin, permissions are shown correctly.

On imported DD, user Adssys is a membre of db:admin.  It does not show any permissions turned on.  AdsSys is a special user that cannot be deleted and cannot change group.  This has to be part of OpenADs core not DAWeb.  OpenAds ace_exports.cpp needs to return an error if Adssys user is (attempted to) change from any client.

On imported DD, user Autotasks has zero permissions dispite the fact that it is a member of General and group general does have some permissions shown.

4. On dialog with DD properties, we need to provide a space for password used for encryption.  We are not importing this field bc it is encrypted itself.  We are not going to try to de-encrypt.  But we do need to provide a space for user to enter one and we do need to save it on our new OpenADS DD.

We should be able to connect with Local server as well as remote as long as the service is up and running.
